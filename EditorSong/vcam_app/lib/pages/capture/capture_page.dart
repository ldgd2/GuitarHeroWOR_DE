// Captura de interpretacion (cuerpo / cabeza y rostro / rostro). El telefono detecta (CaptureEngine.kt) y manda al
// editor lo que ve; el editor lo pone en el personaje, lo graba sobre la cancion y lo exporta a animaciones del juego.
//   movil -> editor  {type:'capmode', mode|null}  {type:'cap', mode, ok, bs, q, lm}  {type:'caprec', on}  {type:'capneutral'}
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../app/theme.dart';
import '../../core/format.dart';
import '../../native/capture_engine.dart';
import '../../net/editor_link.dart';
import '../../widgets/face_painter.dart';
import '../../widgets/model_preview.dart';
import '../../widgets/pose_painter.dart';
import '../../widgets/record_button.dart';
import '../../widgets/status.dart';
import 'capture_mode.dart';

class CapturePage extends StatefulWidget {
  const CapturePage({super.key, required this.link, required this.mode});
  final EditorLink link;
  final CaptureMode mode;
  @override
  State<CapturePage> createState() => _CapturePageState();
}

class _CapturePageState extends State<CapturePage> {
  EditorLink get link => widget.link;
  CaptureMode get mode => widget.mode;

  StreamSubscription? sub;
  bool front = true, precise = false, gpu = false, hands = true, running = false, ok = false, mirror = true, wasRec = false, showModel = true;
  String dev = '';
  int session = -1;
  final preview = PreviewController();
  double smooth = 0.5, fps = 0;
  String status = 'Abriendo la camara...';
  List<double> pts = const [], handPts = const [];
  List<int> rings = const [];
  Map<String, double> bs = const {};
  DateTime lastSend = DateTime.fromMillisecondsSinceEpoch(0);
  DateTime lastPaint = DateTime.fromMillisecondsSinceEpoch(0);

  String get _pref => 'cap_${mode.wire}_';

  @override
  void initState() {
    super.initState();
    front = mode.frontDefault;
    link.addListener(onLink);
    link.send({'type': 'capmode', 'mode': mode.wire});
    sub = CaptureEngine.data().listen(onData);
    SharedPreferences.getInstance().then((p) {
      front = p.getBool('${_pref}front') ?? front;
      precise = p.getBool('${_pref}precise') ?? precise;
      smooth = p.getDouble('${_pref}smooth') ?? smooth;
      gpu = p.getBool('${_pref}gpu') ?? gpu;
      hands = p.getBool('${_pref}hands') ?? hands;
      showModel = p.getBool('cap_show_model') ?? showModel;
      start();
    });
  }

  @override
  void dispose() {
    link.removeListener(onLink);
    if (link.capRec) link.send({'type': 'caprec', 'on': false});
    link.send({'type': 'capmode', 'mode': null});
    sub?.cancel();
    CaptureEngine.stop(session);
    super.dispose();
  }

  void onLink() {
    if (link.capRec != wasRec) { wasRec = link.capRec; HapticFeedback.heavyImpact(); }
    if (mounted) setState(() {});
  }

  Future<void> savePrefs() async {
    final p = await SharedPreferences.getInstance();
    await p.setBool('${_pref}front', front);
    await p.setBool('${_pref}precise', precise);
    await p.setDouble('${_pref}smooth', smooth);
    await p.setBool('${_pref}gpu', gpu);
    await p.setBool('${_pref}hands', hands);
    await p.setBool('cap_show_model', showModel);
  }

  Future<void> start() async {
    if (!mounted) return;
    setState(() { running = false; ok = false; status = 'Abriendo la camara...'; });
    final r = await CaptureEngine.start(mode: mode.wire, front: front, precise: precise, smooth: smooth, gpu: gpu, hands: hands);
    // la pantalla se cerro mientras arrancaba: esa sesion no la quiere nadie
    if (!mounted) { if (r.startsWith('ok')) CaptureEngine.stop(int.tryParse(r.split(':').last) ?? -1); return; }
    setState(() {
      if (r.startsWith('ok')) {
        session = int.tryParse(r.split(':').last) ?? -1;
        running = true;
        status = mode == CaptureMode.body ? 'Buscando un cuerpo...' : 'Buscando una cara...';
      } else if (r == 'permiso') {
        status = 'Hace falta el permiso de camara.';
      } else {
        status = 'No se pudo iniciar la deteccion: $r';
      }
    });
  }

  void onData(Map m) {
    final kind = m['kind'];
    if (kind == 'status') {
      setState(() { running = m['state'] != 'stopped' && m['state'] != 'error' ? running : false; status = '${m['why'] ?? ''}'; });
      return;
    }
    fps = (m['fps'] as num?)?.toDouble() ?? fps;
    dev = (m['dev'] as String?) ?? dev;
    mirror = m['mirror'] == true;
    final wasOk = ok;
    ok = m['ok'] == true;
    final out = <String, dynamic>{'type': 'cap', 'mode': mode.wire, 'ok': ok};
    if (kind == 'face') {
      if (ok) {
        bs = (m['bs'] as Map).map((k, v) => MapEntry(k as String, (v as num).toDouble()));
        out['bs'] = bs.map((k, v) => MapEntry(k, double.parse(v.toStringAsFixed(3))));
        if (m['q'] != null) out['q'] = (m['q'] as List).map((v) => double.parse((v as num).toStringAsFixed(4))).toList();
        pts = (m['pts'] as List).map((v) => (v as num).toDouble()).toList();
        rings = (m['rings'] as List).map((v) => (v as num).toInt()).toList();
        preview.face(out['bs'] as Map<String, double>, out['q'] as List?);
      }
    } else if (kind == 'body') {
      if (ok) {
        out['lm'] = (m['lm'] as List).map((v) => double.parse((v as num).toStringAsFixed(4))).toList();
        pts = (m['pts'] as List).map((v) => (v as num).toDouble()).toList();
        // manos (21 puntos x 3 por lado, del cantante): dedos y agarre del micro en el editor
        final h = m['hands'] as Map?;
        if (h != null) {
          out['hands'] = h.map((k, v) => MapEntry(k as String, (v as List).map((x) => double.parse((x as num).toStringAsFixed(4))).toList()));
        }
        handPts = m['hpts'] == null ? const [] : (m['hpts'] as List).map((v) => (v as num).toDouble()).toList();
        preview.body(out['lm'] as List, out['hands'] as Map?);
      }
    }
    if (!ok && wasOk) preview.lost();
    // al editor: cada resultado (~30/s); si no hay deteccion, solo el cambio de estado y luego 2 por segundo
    final now = DateTime.now();
    if (ok || wasOk || now.difference(lastSend).inMilliseconds > 500) {
      link.send(out);
      lastSend = now;
    }
    if (ok != wasOk) status = ok ? '' : (mode == CaptureMode.body ? 'No veo el cuerpo entero' : 'No veo la cara');
    // los datos ya han salido (editor y personaje) a ritmo completo; la pantalla se repinta como mucho 20 veces/s
    final ms = now.difference(lastPaint).inMilliseconds;
    if (ok != wasOk || ms >= 50) { lastPaint = now; setState(() {}); }
  }

  void toggleRec() {
    if (!ok && !link.capRec) return;
    link.send({'type': 'caprec', 'on': !link.capRec});
    HapticFeedback.heavyImpact();
  }

  void calibrate() {
    link.send({'type': 'capneutral'});
    preview.neutral();
    HapticFeedback.mediumImpact();
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      duration: const Duration(seconds: 2),
      content: Text(mode == CaptureMode.body ? 'Postura de reposo guardada' : 'Cara en reposo guardada'),
    ));
  }

  // ---------------------------------------------------------------------------------------------------------
  @override
  Widget build(BuildContext context) {
    final rec = link.capRec;
    return Row(children: [
      // ---- vista de la deteccion ----
      Expanded(
        child: Container(
          color: const Color(0xFF07080A),
          child: Stack(children: [
            // personaje 3D (vista principal) o los puntos detectados; el otro va en la miniatura
            if (showModel) Positioned.fill(child: ModelPreview(controller: preview, mode: mode.wire))
            else Positioned.fill(child: Padding(padding: const EdgeInsets.all(18), child: _points())),
            if (showModel) Positioned(
              left: 12, bottom: 12, width: 170, height: 120,
              child: GestureDetector(
                onTap: () { setState(() => showModel = false); savePrefs(); },
                child: Container(
                  decoration: BoxDecoration(color: const Color(0xCC07080A), border: Border.all(color: kLine), borderRadius: BorderRadius.circular(6)),
                  padding: const EdgeInsets.all(6),
                  child: _points(),
                ),
              ),
            ),
            if (rec) IgnorePointer(child: Container(decoration: BoxDecoration(border: Border.all(color: kRec, width: 4)))),
            Positioned(
              top: 10, left: 12, right: 12,
              child: Row(children: [
                StatusDot(!link.editorOn ? kRec : ok ? kOk : kAccent),
                const SizedBox(width: 8),
                Text(mode.title, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 16)),
                const SizedBox(width: 12),
                if (rec) Text('GRABANDO  ${fmtTime(link.songT)}', style: const TextStyle(color: kRec, fontWeight: FontWeight.w700))
                else Text(fmtTime(link.songT), style: const TextStyle(color: kMuted, fontFeatures: [FontFeature.tabularFigures()])),
                const Spacer(),
                if (running) InfoChip('${fps.toStringAsFixed(0)} fps${dev.isEmpty ? '' : ' $dev'}', Icons.speed, color: fps >= 20 ? Colors.white70 : kAccent),
                const SizedBox(width: 6),
                IconButton.filledTonal(
                  tooltip: showModel ? 'Ver los puntos detectados' : 'Ver el personaje 3D',
                  icon: Icon(showModel ? Icons.scatter_plot_outlined : Icons.view_in_ar_outlined, size: 20),
                  onPressed: () { setState(() => showModel = !showModel); savePrefs(); },
                ),
              ]),
            ),
            if (status.isNotEmpty) Positioned(bottom: 14, left: 0, right: 0, child: Center(child: WarnBox(status))),
            if (!link.editorOn) const Positioned(bottom: 60, left: 0, right: 0, child: Center(child: WarnBox('El editor no esta abierto en el PC'))),
          ]),
        ),
      ),
      // ---- panel de control ----
      Container(
        width: 290,
        color: kPanel,
        child: SafeArea(
          left: false,
          child: ListView(padding: const EdgeInsets.fromLTRB(16, 12, 16, 16), children: [
            Text(mode.hint, style: const TextStyle(color: kMuted, fontSize: 12)),
            const SizedBox(height: 10),
            SegmentedButton<bool>(
              segments: const [
                ButtonSegment(value: true, icon: Icon(Icons.camera_front_outlined), label: Text('Frontal')),
                ButtonSegment(value: false, icon: Icon(Icons.camera_rear_outlined), label: Text('Trasera')),
              ],
              selected: {front},
              onSelectionChanged: rec ? null : (s) { front = s.first; savePrefs(); start(); },
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero, dense: true,
              title: const Text('Precision alta'),
              subtitle: Text(mode == CaptureMode.body ? 'Modelo pesado y 720p (mas lento)' : 'Imagen a 720p'),
              value: precise,
              onChanged: rec ? null : (v) { precise = v; savePrefs(); start(); },
            ),
            if (mode == CaptureMode.body) SwitchListTile(
              contentPadding: EdgeInsets.zero, dense: true,
              title: const Text('Manos y dedos'),
              subtitle: const Text('Dedos y agarre del micro. Algo mas lento.'),
              value: hands,
              onChanged: rec ? null : (v) { hands = v; savePrefs(); start(); },
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero, dense: true,
              title: const Text('Acelerar con la GPU'),
              subtitle: const Text('Mas rapido, pero en algunos telefonos detecta peor. Si falla, dejalo apagado.'),
              value: gpu,
              onChanged: rec ? null : (v) { gpu = v; savePrefs(); start(); },
            ),
            Text('Suavizado: ${(smooth * 100).round()} %', style: const TextStyle(fontSize: 13)),
            Slider(value: smooth, onChanged: rec ? null : (v) => setState(() => smooth = v), onChangeEnd: (_) { savePrefs(); start(); }),
            const SizedBox(height: 4),
            OutlinedButton.icon(
              onPressed: ok && !rec ? calibrate : null,
              icon: const Icon(Icons.adjust),
              label: Text(mode == CaptureMode.body ? 'Calibrar postura de reposo' : 'Calibrar cara en reposo'),
            ),
            const SizedBox(height: 4),
            Text(mode == CaptureMode.body ? 'De pie, brazos abajo, mirando al telefono.' : 'Cara relajada, boca cerrada, mirando al telefono.',
                style: const TextStyle(color: kMuted, fontSize: 11)),
            if (mode.isFace && ok) ...[
              const SizedBox(height: 12),
              _meter('Boca', bs['jawOpen'] ?? 0),
              _meter('Sonrisa', ((bs['mouthSmileLeft'] ?? 0) + (bs['mouthSmileRight'] ?? 0)) / 2),
              _meter('Cejas', bs['browInnerUp'] ?? 0),
              _meter('Parpados', ((bs['eyeBlinkLeft'] ?? 0) + (bs['eyeBlinkRight'] ?? 0)) / 2),
            ],
            const SizedBox(height: 16),
            Center(child: RecordButton(rec: rec, onTap: toggleRec, size: 76, enabled: rec || (ok && link.editorOn))),
            const SizedBox(height: 6),
            Text(rec ? 'Toca para parar' : 'Graba desde el cursor del editor con la cancion sonando',
                textAlign: TextAlign.center, style: const TextStyle(color: kMuted, fontSize: 11)),
          ]),
        ),
      ),
    ]);
  }

  Widget _points() => CustomPaint(
        size: Size.infinite,
        painter: mode == CaptureMode.body ? PosePainter(pts, ok: ok, mirror: mirror, hands: handPts) : FacePainter(pts, rings, ok: ok, mirror: mirror),
      );

  Widget _meter(String label, double v) => Padding(
        padding: const EdgeInsets.only(bottom: 6),
        child: Row(children: [
          SizedBox(width: 70, child: Text(label, style: const TextStyle(fontSize: 12, color: kMuted))),
          Expanded(child: ClipRRect(
            borderRadius: BorderRadius.circular(2),
            child: LinearProgressIndicator(value: v.clamp(0.0, 1.0), minHeight: 6, color: kAccent, backgroundColor: kLine),
          )),
        ]),
      );
}
