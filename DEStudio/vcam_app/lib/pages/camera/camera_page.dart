// Camara virtual: el telefono mueve la camara del editor y ve su imagen.
// Manda 60 veces por segundo la pose RELATIVA a la de partida:
//   q = giro relativo, en el espacio de la camara de partida (x derecha, y arriba, mirando a -z)
//   p = desplazamiento en un marco NIVELADO (y = arriba de verdad, -z = hacia donde miraba al empezar), en metros
// Con ARCore la posicion es la REAL del telefono (caminar, agacharse, subir); sin ARCore, giroscopio + mando.
// GRABAR: el editor reproduce la cancion desde el inicio de la toma y guarda cada pose como keyframe.
import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:sensors_plus/sensors_plus.dart';

import '../../app/theme.dart';
import '../../core/format.dart';
import '../../core/quat.dart';
import '../../native/ar_tracker.dart';
import '../../net/editor_link.dart';
import '../../widgets/guides_painter.dart';
import '../../widgets/record_button.dart';
import '../../widgets/status.dart';
import 'camera_settings.dart';
import 'camera_sticks.dart';

class CameraPage extends StatefulWidget {
  const CameraPage({super.key, required this.link});
  final EditorLink link;
  @override
  State<CameraPage> createState() => _CameraPageState();
}

class _CameraPageState extends State<CameraPage> {
  EditorLink get link => widget.link;
  final cfg = CameraSettings();

  // seguimiento
  Track track = Track.gyro;
  String arState = 'apagado', arWhy = '';
  Quat? arQ, arBaseQ;
  List<double>? arP, arBaseP;

  // pose enviada (relativa)
  Quat q = Quat();                         // giroscopio: giro integrado
  Quat qOut = Quat();
  final pOut = [0.0, 0.0, 0.0];
  final offset = [0.0, 0.0, 0.0];         // mando + altura (marco nivelado)
  double zoom = 1, zoomStart = 1;
  bool hold = false, wasRec = false;

  // mandos
  Offset stick = Offset.zero;
  double lift = 0;
  double gravX = 9.8;
  DateTime? lastGyro;
  final subs = <StreamSubscription>[];
  StreamSubscription? arSub;
  Timer? tick;
  DateTime lastTick = DateTime.now();

  @override
  void initState() {
    super.initState();
    link.addListener(onLink);
    subs.add(link.events.listen((m) { if (m['type'] == 'recenter') recenter(); }));
    subs.add(gyroscopeEventStream(samplingPeriod: SensorInterval.gameInterval).listen(onGyro));
    subs.add(accelerometerEventStream(samplingPeriod: SensorInterval.uiInterval).listen((e) { gravX = gravX * 0.9 + e.x * 0.1; }));
    tick = Timer.periodic(const Duration(milliseconds: 16), (_) => onTick());
    cfg.load().then((_) {
      if (!mounted) return;
      setState(() {});
      sendQuality();
      if (cfg.ar) startAr();
    });
  }

  @override
  void dispose() {
    link.removeListener(onLink);
    for (final s in subs) { s.cancel(); }
    arSub?.cancel();
    ArTracker.stop();
    tick?.cancel();
    super.dispose();
  }

  void onLink() {
    if (link.rec != wasRec) { wasRec = link.rec; HapticFeedback.heavyImpact(); }
    if (mounted) setState(() {});
  }

  void sendQuality() => link.send({'type': 'quality', 'fps': cfg.fps, 'width': cfg.width});

  // ---- ARCore ----
  Future<void> startAr() async {
    setState(() { arState = 'iniciando'; arWhy = ''; });
    arSub ??= ArTracker.poses().listen(onAr);
    final r = await ArTracker.start();
    if (!mounted) return;
    setState(() {
      switch (r) {
        case 'ok': track = Track.ar; arState = 'iniciando'; arBaseQ = null; arWhy = 'Mueve un poco el telefono para que empiece a seguir tu posicion.';
        case 'permiso': track = Track.gyro; arState = 'sin permiso'; arWhy = 'Hace falta el permiso de camara para seguir tu posicion.';
        case 'instalar': track = Track.gyro; arState = 'instalando'; arWhy = 'Instala "Servicios de Google Play para RA" y vuelve a activar el seguimiento.';
        case 'no_soportado': track = Track.gyro; arState = 'no disponible'; arWhy = 'Este telefono no es compatible con ARCore: se usa el giroscopio.';
        default: track = Track.gyro; arState = 'error'; arWhy = r;
      }
    });
  }

  Future<void> stopAr() async {
    await ArTracker.stop();
    if (!mounted) return;
    setState(() { track = Track.gyro; arState = 'apagado'; arWhy = ''; });
    recenter();
  }

  void onAr(Map m) {
    final st = m['state'] as String? ?? 'stopped';
    final why = st == 'tracking' ? '' : (m['why'] as String? ?? '');
    if (st != arState || (why.isNotEmpty && why != arWhy) || (st == 'tracking' && arWhy.isNotEmpty)) {
      setState(() { arState = st; arWhy = why; });
    }
    if (st != 'tracking' || m['q'] == null) return;   // sin seguimiento: se mantiene la ultima pose (no salta)
    arQ = Quat.list(m['q'] as List);
    arP = (m['p'] as List).map((v) => (v as num).toDouble()).toList();
    if (arBaseQ == null) { arBaseQ = arQ!.copy(); arBaseP = List.of(arP!); }
  }

  // ---- sensores (modo giroscopio) ----
  List<double> toCam(double x, double y, double z) {   // ejes del telefono -> camara, segun el lado del apaisado
    final s = gravX >= 0 ? 1.0 : -1.0;
    return [-s * y, s * x, z];
  }

  void onGyro(GyroscopeEvent e) {
    final now = e.timestamp;
    final dt = lastGyro == null ? 0.0 : now.difference(lastGyro!).inMicroseconds / 1e6;
    lastGyro = now;
    if (track != Track.gyro || hold || dt <= 0 || dt > 0.1) return;
    final w = toCam(e.x, e.y, e.z);
    q = q.mul(Quat.fromRotVec(w[0] * dt * cfg.rotGain, w[1] * dt * cfg.rotGain, w[2] * dt * cfg.rotGain));
    q.normalize();
  }

  // ---- 60 Hz: pose final y envio ----
  void onTick() {
    final now = DateTime.now();
    final dt = math.min(0.05, now.difference(lastTick).inMicroseconds / 1e6);
    lastTick = now;
    Quat target;
    var pos = [0.0, 0.0, 0.0];
    if (track == Track.ar && arQ != null && arBaseQ != null) {
      // giro relativo a la pose de partida, en su propio espacio
      target = arBaseQ!.inv().mul(arQ!);
      // desplazamiento real en metros, girado al rumbo de partida (marco nivelado)
      final d = [arP![0] - arBaseP![0], arP![1] - arBaseP![1], arP![2] - arBaseP![2]];
      pos = Quat.yaw(-arBaseQ!.heading()).rotate(d[0] * cfg.scale, d[1] * cfg.scale, d[2] * cfg.scale);
    } else {
      target = q;
    }
    if (!hold) {
      // mando y altura: en el marco nivelado, segun hacia donde mira la camara ahora
      if (stick != Offset.zero || lift != 0) {
        final v = Quat.yaw(target.heading()).rotate(stick.dx, 0, stick.dy);
        for (var i = 0; i < 3; i++) { offset[i] += v[i] * cfg.moveSpeed * dt; }
        offset[1] += lift * cfg.moveSpeed * 0.6 * dt;
      }
      // estabilizador: suaviza el temblor sin quitar el movimiento
      final a = cfg.stab <= 0.001 ? 1.0 : 1 - math.pow(cfg.stab.clamp(0.0, 0.95), dt * 60).toDouble();
      qOut = qOut.slerpTo(target, a);
      for (var i = 0; i < 3; i++) { pOut[i] += (pos[i] + offset[i] - pOut[i]) * a; }
    }
    link.send({'type': 'pose', 'q': [qOut.x, qOut.y, qOut.z, qOut.w], 'p': pOut, 'zoom': zoom});
  }

  void recenter() {
    q = Quat();
    qOut = Quat();
    arBaseQ = arQ?.copy();
    arBaseP = arP == null ? null : List.of(arP!);
    for (var i = 0; i < 3; i++) { pOut[i] = 0; offset[i] = 0; }
    if (mounted) setState(() { zoom = 1; });
  }

  // "Soltar": mientras se mantiene pulsado la camara se queda quieta y puedes recolocar el telefono; al soltar,
  // sigue desde donde estaba (la pose actual del telefono pasa a ser la nueva referencia)
  void setHold(bool v) {
    if (!v && hold) {
      final keepQ = qOut.copy(), keepP = List.of(pOut);
      if (track == Track.ar && arQ != null) {
        arBaseQ = arQ!.mul(keepQ.inv());   // asi arBase^-1 * arQ = keepQ
        arBaseP = List.of(arP!);
      } else {
        q = keepQ;
      }
      for (var i = 0; i < 3; i++) { offset[i] = keepP[i]; }
    }
    setState(() => hold = v);
  }

  void toggleRec() {
    if (!link.rec) recenter();
    link.send({'type': 'rec', 'on': !link.rec});
    HapticFeedback.heavyImpact();
  }

  void openSettings() => showCameraSettings(context, cfg,
      onAr: (v) => v ? startAr() : stopAr(), onQuality: sendQuality, onChanged: () => setState(() {}), onDisconnect: link.close);

  // ---------------------------------------------------------------------------------------------------------
  @override
  Widget build(BuildContext context) {
    final sh = link.shot, rec = link.rec;
    final dur = (sh?['dur'] as num?)?.toInt() ?? 0, st = (sh?['t'] as num?)?.toInt() ?? 0;
    final progress = rec && dur > 0 ? ((link.songT - st) / dur).clamp(0.0, 1.0) : null;
    final stale = link.frames > 0 && DateTime.now().difference(link.lastFrameAt).inSeconds > 3;
    final showWhy = arWhy.isNotEmpty && !(track == Track.ar && arState == 'tracking');
    return Stack(children: [
      // ---- visor 16:9 ----
      Positioned.fill(
        child: GestureDetector(
          onScaleStart: (_) => zoomStart = zoom,
          onScaleUpdate: (d) { if (d.pointerCount >= 2) setState(() { zoom = (zoomStart * d.scale).clamp(0.3, 8.0); }); },
          child: Center(
            child: AspectRatio(
              aspectRatio: 16 / 9,
              child: Stack(fit: StackFit.expand, children: [
                Container(color: const Color(0xFF0B0C10)),
                if (link.frame != null) Image.memory(link.frame!, gaplessPlayback: true, fit: BoxFit.cover, filterQuality: FilterQuality.low),
                if (cfg.grid) IgnorePointer(child: CustomPaint(painter: ThirdsGuides())),
                if (link.frame == null || stale) _noImage(),
                if (rec) IgnorePointer(child: Container(decoration: BoxDecoration(border: Border.all(color: kRec, width: 4)))),
              ]),
            ),
          ),
        ),
      ),
      // ---- barra superior ----
      Positioned(
        top: 0, left: 0, right: 0,
        child: Container(
          padding: const EdgeInsets.fromLTRB(14, 8, 6, 14),
          color: const Color(0x99000000),
          child: Row(children: [
            StatusDot(link.editorOn ? kOk : kRec),
            const SizedBox(width: 8),
            Flexible(
              child: Text(
                rec ? 'GRABANDO  ${link.keys} claves' : sh != null ? 'Toma ${sh['id']}, empieza en ${fmtTime(st)}' : 'Sin toma: al grabar se crea una en el cursor',
                overflow: TextOverflow.ellipsis,
                style: TextStyle(color: rec ? kRec : Colors.white, fontWeight: FontWeight.w700),
              ),
            ),
            const SizedBox(width: 12),
            Text(fmtTime(link.songT), style: const TextStyle(fontFeatures: [FontFeature.tabularFigures()], fontSize: 20, fontWeight: FontWeight.w700)),
            const SizedBox(width: 12),
            _trackChip(),
            const SizedBox(width: 6),
            InfoChip('x${zoom.toStringAsFixed(2)}', Icons.zoom_in),
            IconButton(onPressed: openSettings, icon: const Icon(Icons.tune, color: Colors.white)),
          ]),
        ),
      ),
      if (progress != null)
        Positioned(top: 0, left: 0, right: 0, child: LinearProgressIndicator(value: progress, minHeight: 3, color: kRec, backgroundColor: Colors.transparent)),
      if (showWhy) Positioned(top: 54, left: 0, right: 0, child: Center(child: WarnBox(arWhy))),
      // ---- mandos de movimiento (izquierda) ----
      Positioned(left: 18, bottom: 18, child: Row(crossAxisAlignment: CrossAxisAlignment.end, children: [
        Stick(onChange: (o) => stick = o),
        const SizedBox(width: 14),
        Lift(onChange: (v) => lift = v),
      ])),
      // ---- zoom (vertical, a la derecha) ----
      Positioned(
        right: 118, bottom: 20, top: 70,
        child: RotatedBox(
          quarterTurns: 3,
          child: Slider(
            value: (math.log(zoom) / math.ln2).clamp(math.log(0.3) / math.ln2, 3.0),
            min: math.log(0.3) / math.ln2, max: 3,
            onChanged: (v) => setState(() => zoom = math.pow(2, v).toDouble()),
            onChangeEnd: (_) => HapticFeedback.selectionClick(),
          ),
        ),
      ),
      // ---- grabar y acciones (derecha) ----
      Positioned(
        right: 16, bottom: 16,
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          RoundAction(Icons.center_focus_strong, 'Recentrar', recenter),
          const SizedBox(height: 10),
          RoundAction(link.live ? Icons.visibility : Icons.visibility_outlined, link.live ? 'En vivo' : 'Ensayo', () {
            link.send({'type': 'live', 'on': !link.live}); setState(() => link.live = !link.live);
          }, on: link.live),
          const SizedBox(height: 10),
          Listener(
            onPointerDown: (_) => setHold(true),
            onPointerUp: (_) => setHold(false),
            onPointerCancel: (_) => setHold(false),
            child: RoundAction(Icons.pan_tool_alt, 'Soltar', null, on: hold),
          ),
          const SizedBox(height: 14),
          RecordButton(rec: rec, onTap: toggleRec),
        ]),
      ),
    ]);
  }

  Widget _noImage() => Container(
        color: Colors.black54,
        alignment: Alignment.center,
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          const Icon(Icons.videocam_off_outlined, size: 40, color: Colors.white54),
          const SizedBox(height: 8),
          Text(!link.editorOn ? 'El editor no esta abierto en el PC' : link.frames == 0 ? 'Esperando la imagen del editor...' : 'La imagen se corto',
              style: const TextStyle(fontWeight: FontWeight.w700)),
          const SizedBox(height: 4),
          const Text('Abre un proyecto en DE Studio (la seccion Camaras tiene la conexion).', style: TextStyle(color: Colors.white60, fontSize: 12)),
        ]),
      );

  Widget _trackChip() {
    final ok = track == Track.ar && arState == 'tracking';
    final label = track == Track.ar ? (ok ? 'Posicion real' : 'Buscando...') : 'Giroscopio';
    final c = track == Track.ar ? (ok ? kOk : kAccent) : Colors.white70;
    return InfoChip(label, track == Track.ar ? Icons.directions_walk : Icons.screen_rotation_alt, color: c,
        onTap: () => track == Track.ar ? stopAr() : startAr());
  }
}
