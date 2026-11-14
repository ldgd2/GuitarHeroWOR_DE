// EditorSong Camara: el telefono como camara virtual del editor de canciones.
//
// - Se conecta al puente del editor (EditorSong/editor/server/vcam.py) por la wifi: ws://IP:PUERTO/phone?pin=PIN
// - Muestra lo que ve la camara (fotogramas JPEG del monitor del editor).
// - Manda 60 veces por segundo la pose RELATIVA a la de partida, en el espacio de la camara
//   (x derecha, y arriba, mirando a -z):
//     giro  = giroscopio integrado (girar el telefono = girar la camara)
//     sitio = mando virtual (avanzar/retroceder/lateral), deslizador de altura y el pulso de la mano (acelerometro)
//     zoom  = pellizco o botones
// - GRABAR: el editor reproduce la cancion desde el inicio de la toma elegida y guarda cada pose como keyframe.
import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:sensors_plus/sensors_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([DeviceOrientation.landscapeLeft, DeviceOrientation.landscapeRight]);
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  runApp(const VcamApp());
}

class VcamApp extends StatelessWidget {
  const VcamApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'EditorSong Camara',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFFE66BD1), brightness: Brightness.dark), useMaterial3: true),
        home: const ConnectPage(),
      );
}

// ---------------------------------------------------------------------------------------------------------
// Conexion
class ConnectPage extends StatefulWidget {
  const ConnectPage({super.key, this.message});
  final String? message;
  @override
  State<ConnectPage> createState() => _ConnectPageState();
}

class _ConnectPageState extends State<ConnectPage> {
  final ip = TextEditingController(), port = TextEditingController(text: '8766'), pin = TextEditingController();
  bool busy = false;
  String? error;

  @override
  void initState() {
    super.initState();
    error = widget.message;
    SharedPreferences.getInstance().then((p) {
      ip.text = p.getString('ip') ?? '';
      port.text = p.getString('port') ?? '8766';
      pin.text = p.getString('pin') ?? '';
    });
  }

  Future<void> connect() async {
    setState(() { busy = true; error = null; });
    final uri = Uri.parse('ws://${ip.text.trim()}:${port.text.trim()}/phone?pin=${pin.text.trim()}');
    try {
      final ch = WebSocketChannel.connect(uri);
      await ch.ready.timeout(const Duration(seconds: 5));
      final p = await SharedPreferences.getInstance();
      await p.setString('ip', ip.text.trim());
      await p.setString('port', port.text.trim());
      await p.setString('pin', pin.text.trim());
      if (!mounted) return;
      Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => CameraPage(channel: ch)));
    } catch (e) {
      setState(() { busy = false; error = 'No se pudo conectar. Revisa la IP, el PIN y que el PC y el telefono esten en la misma wifi.'; });
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        body: SafeArea(
          child: Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(24),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 520),
                child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                  const Text('EditorSong · Camara del movil', style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
                  const SizedBox(height: 6),
                  const Text('Escribe lo que muestra el editor en Camaras › Camara del movil.', style: TextStyle(color: Colors.white70)),
                  const SizedBox(height: 16),
                  Row(children: [
                    Expanded(flex: 3, child: TextField(controller: ip, decoration: const InputDecoration(labelText: 'IP del PC', hintText: '192.168.1.20'), keyboardType: TextInputType.url)),
                    const SizedBox(width: 12),
                    Expanded(child: TextField(controller: port, decoration: const InputDecoration(labelText: 'Puerto'), keyboardType: TextInputType.number)),
                    const SizedBox(width: 12),
                    Expanded(child: TextField(controller: pin, decoration: const InputDecoration(labelText: 'PIN'), keyboardType: TextInputType.number)),
                  ]),
                  const SizedBox(height: 20),
                  FilledButton.icon(onPressed: busy ? null : connect, icon: const Icon(Icons.videocam), label: Text(busy ? 'Conectando...' : 'Conectar')),
                  if (error != null) Padding(padding: const EdgeInsets.only(top: 12), child: Text(error!, style: const TextStyle(color: Colors.redAccent))),
                ]),
              ),
            ),
          ),
        ),
      );
}

// ---------------------------------------------------------------------------------------------------------
// Cuaternion minimo (x, y, z, w)
class Quat {
  double x, y, z, w;
  Quat([this.x = 0, this.y = 0, this.z = 0, this.w = 1]);
  Quat mul(Quat b) => Quat(
        w * b.x + x * b.w + y * b.z - z * b.y,
        w * b.y - x * b.z + y * b.w + z * b.x,
        w * b.z + x * b.y - y * b.x + z * b.w,
        w * b.w - x * b.x - y * b.y - z * b.z,
      );
  void normalize() {
    final n = math.sqrt(x * x + y * y + z * z + w * w);
    if (n > 0) { x /= n; y /= n; z /= n; w /= n; }
  }
  static Quat fromRotVec(double ax, double ay, double az) {
    final a = math.sqrt(ax * ax + ay * ay + az * az);
    if (a < 1e-9) return Quat();
    final s = math.sin(a / 2) / a;
    return Quat(ax * s, ay * s, az * s, math.cos(a / 2));
  }
  List<double> rotate(double vx, double vy, double vz) {   // q * v * q^-1
    final tx = 2 * (y * vz - z * vy), ty = 2 * (z * vx - x * vz), tz = 2 * (x * vy - y * vx);
    return [vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx)];
  }
}

// ---------------------------------------------------------------------------------------------------------
// Camara
class CameraPage extends StatefulWidget {
  const CameraPage({super.key, required this.channel});
  final WebSocketChannel channel;
  @override
  State<CameraPage> createState() => _CameraPageState();
}

class _CameraPageState extends State<CameraPage> {
  Uint8List? frame;
  bool editorOn = true, rec = false, live = false, playing = false;
  int songT = 0, keys = 0;
  Map<String, dynamic>? shot;

  // pose relativa
  Quat q = Quat();
  final p = [0.0, 0.0, 0.0];
  double zoom = 1, zoomStart = 1;
  // ajustes
  double moveSpeed = 1.5, shakeAmt = 1.0, rotGain = 1.0;
  bool gyroOn = true, hold = false;
  int fps = 12;
  // mandos
  Offset stick = Offset.zero;
  double lift = 0;
  // sensores
  double gravX = 9.8;                      // gravedad en el eje x del telefono: dice hacia que lado esta girado
  final shakeV = [0.0, 0.0, 0.0], shakeP = [0.0, 0.0, 0.0];
  DateTime? lastGyro, lastAcc;
  final subs = <StreamSubscription>[];
  Timer? tick;
  DateTime lastTick = DateTime.now();

  @override
  void initState() {
    super.initState();
    WakelockPlus.enable();
    subs.add(widget.channel.stream.listen(onMessage, onDone: onClosed, onError: (_) => onClosed()));
    subs.add(gyroscopeEventStream(samplingPeriod: SensorInterval.gameInterval).listen(onGyro));
    subs.add(userAccelerometerEventStream(samplingPeriod: SensorInterval.gameInterval).listen(onAccel));
    subs.add(accelerometerEventStream(samplingPeriod: SensorInterval.uiInterval).listen((e) { gravX = gravX * 0.9 + e.x * 0.1; }));
    tick = Timer.periodic(const Duration(milliseconds: 16), (_) => onTick());
    send({'type': 'quality', 'fps': fps});
  }

  @override
  void dispose() {
    for (final s in subs) { s.cancel(); }
    tick?.cancel();
    WakelockPlus.disable();
    widget.channel.sink.close();
    super.dispose();
  }

  void send(Map<String, dynamic> m) {
    try { widget.channel.sink.add(jsonEncode(m)); } catch (_) {}
  }

  void onClosed() {
    if (!mounted) return;
    Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => const ConnectPage(message: 'Se perdio la conexion con el editor.')));
  }

  void onMessage(dynamic data) {
    if (data is List<int>) {
      setState(() { frame = Uint8List.fromList(data); });
      return;
    }
    try {
      final m = jsonDecode(data as String) as Map<String, dynamic>;
      switch (m['type']) {
        case 'link':
          setState(() { editorOn = m['editor'] == true; });
        case 'state':
          setState(() {
            rec = m['rec'] == true; live = m['live'] == true; playing = m['playing'] == true;
            songT = (m['t'] as num?)?.toInt() ?? 0; keys = (m['keys'] as num?)?.toInt() ?? 0;
            shot = m['shot'] as Map<String, dynamic>?;
          });
        case 'recenter':
          recenter();
        case 'live':
          setState(() { live = m['on'] == true; });
      }
    } catch (_) {}
  }

  // eje del telefono -> eje de la camara, segun hacia que lado esta girado en horizontal
  List<double> toCam(double x, double y, double z) {
    final s = gravX >= 0 ? 1.0 : -1.0;
    return [-s * y, s * x, z];
  }

  void onGyro(GyroscopeEvent e) {
    final now = e.timestamp;
    final dt = lastGyro == null ? 0.0 : now.difference(lastGyro!).inMicroseconds / 1e6;
    lastGyro = now;
    if (!gyroOn || hold || dt <= 0 || dt > 0.1) return;
    final w = toCam(e.x, e.y, e.z);
    q = q.mul(Quat.fromRotVec(w[0] * dt * rotGain, w[1] * dt * rotGain, w[2] * dt * rotGain));
    q.normalize();
  }

  void onAccel(UserAccelerometerEvent e) {
    final now = e.timestamp;
    final dt = lastAcc == null ? 0.0 : now.difference(lastAcc!).inMicroseconds / 1e6;
    lastAcc = now;
    if (dt <= 0 || dt > 0.1) return;
    // pulso de la mano: la aceleracion empuja un poco la camara y vuelve sola al centro (no se acumula)
    final a = toCam(e.x, e.y, e.z);
    final damp = math.exp(-5 * dt), leak = math.exp(-2.5 * dt);
    for (var i = 0; i < 3; i++) {
      shakeV[i] = (shakeV[i] + a[i] * dt * 0.35 * shakeAmt) * damp;
      shakeP[i] = (shakeP[i] + shakeV[i] * dt) * leak;
    }
  }

  void onTick() {
    final now = DateTime.now();
    final dt = math.min(0.05, now.difference(lastTick).inMicroseconds / 1e6);
    lastTick = now;
    if (!hold && (stick != Offset.zero || lift != 0)) {
      // mando: avanzar hacia donde mira la camara; altura en vertical
      final v = q.rotate(stick.dx, 0, stick.dy);
      for (var i = 0; i < 3; i++) { p[i] += v[i] * moveSpeed * dt; }
      p[1] += lift * moveSpeed * 0.6 * dt;
    }
    final pos = [for (var i = 0; i < 3; i++) p[i] + shakeP[i]];
    send({'type': 'pose', 'q': [q.x, q.y, q.z, q.w], 'p': pos, 'zoom': zoom});
  }

  void recenter() {
    q = Quat();
    for (var i = 0; i < 3; i++) { p[i] = 0; shakeV[i] = 0; shakeP[i] = 0; }
    setState(() { zoom = 1; });
  }

  void toggleRec() {
    if (!rec) recenter();
    send({'type': 'rec', 'on': !rec});
    HapticFeedback.heavyImpact();
  }

  String fmt(int ms) {
    final s = ms / 1000.0;
    return '${(s ~/ 60)}:${(s % 60).toStringAsFixed(1).padLeft(4, '0')}';
  }

  @override
  Widget build(BuildContext context) {
    final sh = shot;
    final info = rec
        ? '● GRABANDO  ${fmt(songT)}  ·  $keys claves'
        : sh != null
            ? 'Toma ${sh['id']} lista · empieza en ${fmt((sh['t'] as num).toInt())}'
            : 'Sin toma elegida: al grabar se crea una nueva en el cursor';
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(children: [
        // imagen del monitor + pellizco para zoom
        Positioned.fill(
          child: GestureDetector(
            onScaleStart: (_) => zoomStart = zoom,
            onScaleUpdate: (d) { if (d.pointerCount >= 2) setState(() { zoom = (zoomStart * d.scale).clamp(0.3, 8.0); }); },
            child: frame == null
                ? Center(child: Text(editorOn ? 'Esperando imagen del editor...' : 'El editor no esta abierto', style: const TextStyle(color: Colors.white54)))
                : Image.memory(frame!, gaplessPlayback: true, fit: BoxFit.contain),
          ),
        ),
        // estado
        Positioned(
          top: 8, left: 12, right: 12,
          child: Row(children: [
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
              decoration: BoxDecoration(color: rec ? Colors.red.withValues(alpha: 0.8) : Colors.black54, borderRadius: BorderRadius.circular(6)),
              child: Text(info, style: const TextStyle(color: Colors.white, fontWeight: FontWeight.w600)),
            ),
            const Spacer(),
            _chip('x${zoom.toStringAsFixed(2)}'),
            IconButton(onPressed: openSettings, icon: const Icon(Icons.tune, color: Colors.white)),
          ]),
        ),
        // mando de movimiento (izquierda)
        Positioned(left: 20, bottom: 20, child: _Stick(onChange: (o) => stick = Offset(o.dx, o.dy))),
        // altura (junto al mando)
        Positioned(left: 170, bottom: 20, child: _Lift(onChange: (v) => lift = v)),
        // botones (derecha)
        Positioned(
          right: 16, bottom: 16,
          child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.end, children: [
            Row(mainAxisSize: MainAxisSize.min, children: [
              _round(Icons.zoom_out, () => setState(() { zoom = (zoom / 1.25).clamp(0.3, 8.0); })),
              const SizedBox(width: 8),
              _round(Icons.zoom_in, () => setState(() { zoom = (zoom * 1.25).clamp(0.3, 8.0); })),
            ]),
            const SizedBox(height: 10),
            Row(mainAxisSize: MainAxisSize.min, children: [
              Listener(
                onPointerDown: (_) => setState(() => hold = true),
                onPointerUp: (_) => setState(() => hold = false),
                onPointerCancel: (_) => setState(() => hold = false),
                child: _pill(hold ? 'Suelto' : 'Mantener: soltar', hold ? Colors.amber : Colors.white24),
              ),
              const SizedBox(width: 8),
              GestureDetector(onTap: recenter, child: _pill('Recentrar', Colors.white24)),
              const SizedBox(width: 8),
              GestureDetector(
                onTap: () { send({'type': 'live', 'on': !live}); setState(() => live = !live); },
                child: _pill(live ? 'En vivo ●' : 'En vivo', live ? Colors.teal : Colors.white24),
              ),
            ]),
            const SizedBox(height: 12),
            GestureDetector(
              onTap: toggleRec,
              child: Container(
                width: 84, height: 84,
                decoration: BoxDecoration(shape: BoxShape.circle, border: Border.all(color: Colors.white, width: 4)),
                child: Center(child: AnimatedContainer(
                  duration: const Duration(milliseconds: 150),
                  width: rec ? 34 : 64, height: rec ? 34 : 64,
                  decoration: BoxDecoration(color: Colors.red, borderRadius: BorderRadius.circular(rec ? 6 : 40)),
                )),
              ),
            ),
          ]),
        ),
      ]),
    );
  }

  Widget _chip(String t) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
        decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(6)),
        child: Text(t, style: const TextStyle(color: Colors.white)),
      );
  Widget _pill(String t, Color c) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
        decoration: BoxDecoration(color: c, borderRadius: BorderRadius.circular(20)),
        child: Text(t, style: const TextStyle(color: Colors.white, fontWeight: FontWeight.w600)),
      );
  Widget _round(IconData i, VoidCallback f) => Material(
        color: Colors.white24, shape: const CircleBorder(),
        child: IconButton(onPressed: f, icon: Icon(i, color: Colors.white)),
      );

  void openSettings() {
    showModalBottomSheet(
      context: context,
      backgroundColor: const Color(0xEE16181D),
      builder: (_) => StatefulBuilder(builder: (ctx, set) {
        Widget slider(String label, double v, double min, double max, ValueChanged<double> f) => Row(children: [
              SizedBox(width: 200, child: Text('$label: ${v.toStringAsFixed(2)}')),
              Expanded(child: Slider(value: v, min: min, max: max, onChanged: (x) { set(() => f(x)); setState(() {}); })),
            ]);
        return Padding(
          padding: const EdgeInsets.all(16),
          child: SingleChildScrollView(child: Column(mainAxisSize: MainAxisSize.min, children: [
            SwitchListTile(title: const Text('Girar con el giroscopio'), value: gyroOn, onChanged: (v) { set(() => gyroOn = v); }),
            slider('Sensibilidad del giro', rotGain, 0.2, 2, (v) => rotGain = v),
            slider('Velocidad del mando (m/s)', moveSpeed, 0.2, 6, (v) => moveSpeed = v),
            slider('Pulso de la mano', shakeAmt, 0, 3, (v) => shakeAmt = v),
            slider('Imagenes por segundo', fps.toDouble(), 4, 30, (v) { fps = v.round(); send({'type': 'quality', 'fps': fps}); }),
            TextButton.icon(
              onPressed: () { Navigator.of(ctx).pop(); widget.channel.sink.close(); },
              icon: const Icon(Icons.link_off), label: const Text('Desconectar'),
            ),
          ])),
        );
      }),
    );
  }
}

// Mando virtual: x = lateral, y = adelante (arriba en pantalla = avanzar = -z)
class _Stick extends StatefulWidget {
  const _Stick({required this.onChange});
  final ValueChanged<Offset> onChange;
  @override
  State<_Stick> createState() => _StickState();
}

class _StickState extends State<_Stick> {
  Offset k = Offset.zero;
  static const r = 60.0;
  void upd(Offset local) {
    var d = local - const Offset(r, r);
    if (d.distance > r) d = d / d.distance * r;
    setState(() => k = d);
    widget.onChange(Offset(d.dx / r, d.dy / r));   // y de pantalla hacia abajo = +z = retroceder
  }
  @override
  Widget build(BuildContext context) => GestureDetector(
        onPanStart: (d) => upd(d.localPosition),
        onPanUpdate: (d) => upd(d.localPosition),
        onPanEnd: (_) { setState(() => k = Offset.zero); widget.onChange(Offset.zero); },
        child: Container(
          width: r * 2, height: r * 2,
          decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.white10, border: Border.all(color: Colors.white38)),
          child: Transform.translate(offset: k, child: Center(child: Container(width: 46, height: 46,
              decoration: const BoxDecoration(shape: BoxShape.circle, color: Colors.white54)))),
        ),
      );
}

// Deslizador de altura que vuelve al centro: arriba = subir
class _Lift extends StatefulWidget {
  const _Lift({required this.onChange});
  final ValueChanged<double> onChange;
  @override
  State<_Lift> createState() => _LiftState();
}

class _LiftState extends State<_Lift> {
  double v = 0;
  static const h = 120.0;
  void upd(double y) {
    final x = ((h / 2 - y) / (h / 2)).clamp(-1.0, 1.0);
    setState(() => v = x);
    widget.onChange(x);
  }
  @override
  Widget build(BuildContext context) => GestureDetector(
        onVerticalDragStart: (d) => upd(d.localPosition.dy),
        onVerticalDragUpdate: (d) => upd(d.localPosition.dy),
        onVerticalDragEnd: (_) { setState(() => v = 0); widget.onChange(0); },
        child: Container(
          width: 44, height: h,
          decoration: BoxDecoration(color: Colors.white10, borderRadius: BorderRadius.circular(22), border: Border.all(color: Colors.white38)),
          child: Stack(children: [
            const Center(child: Icon(Icons.unfold_more, color: Colors.white38)),
            Positioned(left: 4, top: h / 2 - 18 - v * (h / 2 - 18), child: Container(width: 36, height: 36,
                decoration: const BoxDecoration(shape: BoxShape.circle, color: Colors.white54))),
          ]),
        ),
      );
}
