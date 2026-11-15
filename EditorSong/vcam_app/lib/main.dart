// EditorSong Camara: el telefono como camara virtual del editor de canciones.
//
// - Se conecta al puente del editor (EditorSong/editor/server/vcam.py) por la wifi: ws://IP:PUERTO/phone?pin=PIN
// - Muestra lo que ve la camara (fotogramas JPEG del monitor del editor).
// - Manda 60 veces por segundo la pose RELATIVA a la de partida:
//     q = giro relativo, en el espacio de la camara de partida (x derecha, y arriba, mirando a -z)
//     p = desplazamiento en un marco NIVELADO (y = arriba de verdad, -z = hacia donde miraba al empezar), en metros
//   Con ARCore la posicion es la REAL del telefono (caminar, agacharse, subir); sin ARCore, giroscopio + mando.
// - GRABAR: el editor reproduce la cancion desde el inicio de la toma y guarda cada pose como keyframe.
import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:sensors_plus/sensors_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

const kAccent = Color(0xFFE66BD1);
const kRec = Color(0xFFFF3B3B);

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
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(seedColor: kAccent, brightness: Brightness.dark),
          useMaterial3: true,
        ),
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
      setState(() {
        busy = false;
        error = 'No se pudo conectar. Revisa la IP y el PIN (cambia cada vez que abres el editor) y que el PC y el '
            'telefono esten en la misma wifi.';
      });
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        body: SafeArea(
          child: Row(children: [
            Expanded(
              child: Container(
                decoration: const BoxDecoration(gradient: LinearGradient(colors: [Color(0xFF2A1030), Color(0xFF0E0F14)], begin: Alignment.topLeft, end: Alignment.bottomRight)),
                padding: const EdgeInsets.all(28),
                child: const Column(crossAxisAlignment: CrossAxisAlignment.start, mainAxisAlignment: MainAxisAlignment.center, children: [
                  Icon(Icons.videocam_rounded, size: 56, color: kAccent),
                  SizedBox(height: 12),
                  Text('Camara del movil', style: TextStyle(fontSize: 28, fontWeight: FontWeight.w800)),
                  SizedBox(height: 8),
                  Text('Mueve el telefono y la camara del editor lo sigue: girar, caminar, agacharte, acercarte.',
                      style: TextStyle(color: Colors.white70, fontSize: 15)),
                  SizedBox(height: 16),
                  _Step(n: 1, t: 'Abre EditorSong en el PC.'),
                  _Step(n: 2, t: 'Pestana Camaras > Camara del movil: alli estan la IP y el PIN.'),
                  _Step(n: 3, t: 'El PC y el telefono, en la misma wifi.'),
                ]),
              ),
            ),
            Expanded(
              child: Center(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.all(28),
                  child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                    TextField(controller: ip, decoration: const InputDecoration(labelText: 'IP del PC', hintText: '192.168.1.20', prefixIcon: Icon(Icons.computer)), keyboardType: TextInputType.url),
                    const SizedBox(height: 12),
                    Row(children: [
                      Expanded(child: TextField(controller: port, decoration: const InputDecoration(labelText: 'Puerto', prefixIcon: Icon(Icons.numbers)), keyboardType: TextInputType.number)),
                      const SizedBox(width: 12),
                      Expanded(child: TextField(controller: pin, decoration: const InputDecoration(labelText: 'PIN', prefixIcon: Icon(Icons.lock_outline)), keyboardType: TextInputType.number)),
                    ]),
                    const SizedBox(height: 20),
                    FilledButton.icon(
                      style: FilledButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 16)),
                      onPressed: busy ? null : connect,
                      icon: busy ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.link),
                      label: Text(busy ? 'Conectando...' : 'Conectar'),
                    ),
                    if (error != null) Padding(padding: const EdgeInsets.only(top: 12), child: Text(error!, style: const TextStyle(color: Colors.redAccent))),
                  ]),
                ),
              ),
            ),
          ]),
        ),
      );
}

class _Step extends StatelessWidget {
  const _Step({required this.n, required this.t});
  final int n;
  final String t;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(top: 6),
        child: Row(children: [
          CircleAvatar(radius: 11, backgroundColor: kAccent, child: Text('$n', style: const TextStyle(fontSize: 12, color: Colors.black, fontWeight: FontWeight.bold))),
          const SizedBox(width: 10),
          Expanded(child: Text(t, style: const TextStyle(color: Colors.white70))),
        ]),
      );
}

// ---------------------------------------------------------------------------------------------------------
// Cuaternion minimo (x, y, z, w)
class Quat {
  double x, y, z, w;
  Quat([this.x = 0, this.y = 0, this.z = 0, this.w = 1]);
  Quat.list(List l) : x = (l[0] as num).toDouble(), y = (l[1] as num).toDouble(), z = (l[2] as num).toDouble(), w = (l[3] as num).toDouble();
  Quat copy() => Quat(x, y, z, w);
  Quat mul(Quat b) => Quat(
        w * b.x + x * b.w + y * b.z - z * b.y,
        w * b.y - x * b.z + y * b.w + z * b.x,
        w * b.z + x * b.y - y * b.x + z * b.w,
        w * b.w - x * b.x - y * b.y - z * b.z,
      );
  Quat inv() => Quat(-x, -y, -z, w);
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
  static Quat yaw(double a) => Quat(0, math.sin(a / 2), 0, math.cos(a / 2));
  List<double> rotate(double vx, double vy, double vz) {   // q * v * q^-1
    final tx = 2 * (y * vz - z * vy), ty = 2 * (z * vx - x * vz), tz = 2 * (x * vy - y * vx);
    return [vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx)];
  }
  // rumbo (giro alrededor de Y) de hacia donde mira (-Z)
  double heading() {
    final f = rotate(0, 0, -1);
    return math.atan2(-f[0], -f[2]);
  }
  Quat slerpTo(Quat b, double t) {
    var d = x * b.x + y * b.y + z * b.z + w * b.w;
    final s = d < 0 ? -1.0 : 1.0;
    d = d.abs();
    if (d > 0.9995) {
      final r = Quat(x + (s * b.x - x) * t, y + (s * b.y - y) * t, z + (s * b.z - z) * t, w + (s * b.w - w) * t);
      r.normalize();
      return r;
    }
    final th = math.acos(d), sn = math.sin(th), a = math.sin((1 - t) * th) / sn, c = s * math.sin(t * th) / sn;
    return Quat(x * a + b.x * c, y * a + b.y * c, z * a + b.z * c, w * a + b.w * c);
  }
}

// Seguimiento ARCore (codigo nativo en android/.../MainActivity.kt)
class ArTracker {
  static const _m = MethodChannel('vcam/ar');
  static const _e = EventChannel('vcam/ar/pose');
  static Future<String> start() async {
    try { return (await _m.invokeMethod<String>('start')) ?? 'error'; } catch (e) { return 'error:$e'; }
  }
  static Future<void> stop() async { try { await _m.invokeMethod('stop'); } catch (_) {} }
  static Stream<Map> poses() => _e.receiveBroadcastStream().map((e) => e as Map);
}

enum Track { ar, gyro }

// ---------------------------------------------------------------------------------------------------------
// Camara
class CameraPage extends StatefulWidget {
  const CameraPage({super.key, required this.channel});
  final WebSocketChannel channel;
  @override
  State<CameraPage> createState() => _CameraPageState();
}

class _CameraPageState extends State<CameraPage> {
  // editor
  Uint8List? frame;
  bool editorOn = true, rec = false, live = false, playing = false;
  int songT = 0, keys = 0, frames = 0;
  Map<String, dynamic>? shot;
  DateTime lastFrameAt = DateTime.fromMillisecondsSinceEpoch(0);

  // seguimiento
  Track track = Track.gyro;
  String arState = 'apagado', arWhy = '';
  bool wantAr = true;
  Quat? arQ, arBaseQ;
  List<double>? arP, arBaseP;

  // pose enviada (relativa)
  Quat q = Quat();                         // giroscopio: giro integrado
  Quat qOut = Quat();
  final pOut = [0.0, 0.0, 0.0];
  final offset = [0.0, 0.0, 0.0];         // mando + altura (marco nivelado)
  double zoom = 1, zoomStart = 1;

  // ajustes
  double moveSpeed = 1.5, rotGain = 1.0, scale = 1.0, stab = 0.35;
  int fps = 15, width = 640;
  bool grid = true, hold = false;

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
    WakelockPlus.enable();
    subs.add(widget.channel.stream.listen(onMessage, onDone: onClosed, onError: (_) => onClosed()));
    subs.add(gyroscopeEventStream(samplingPeriod: SensorInterval.gameInterval).listen(onGyro));
    subs.add(accelerometerEventStream(samplingPeriod: SensorInterval.uiInterval).listen((e) { gravX = gravX * 0.9 + e.x * 0.1; }));
    tick = Timer.periodic(const Duration(milliseconds: 16), (_) => onTick());
    SharedPreferences.getInstance().then((p) {
      if (!mounted) return;
      setState(() {
        moveSpeed = p.getDouble('moveSpeed') ?? moveSpeed;
        rotGain = p.getDouble('rotGain') ?? rotGain;
        scale = p.getDouble('scale') ?? scale;
        stab = p.getDouble('stab') ?? stab;
        fps = p.getInt('fps') ?? fps;
        width = p.getInt('width') ?? width;
        grid = p.getBool('grid') ?? grid;
        wantAr = p.getBool('ar') ?? wantAr;
      });
      sendQuality();
      if (wantAr) startAr();
    });
  }

  @override
  void dispose() {
    for (final s in subs) { s.cancel(); }
    arSub?.cancel();
    ArTracker.stop();
    tick?.cancel();
    WakelockPlus.disable();
    widget.channel.sink.close();
    super.dispose();
  }

  Future<void> savePrefs() async {
    final p = await SharedPreferences.getInstance();
    await p.setDouble('moveSpeed', moveSpeed); await p.setDouble('rotGain', rotGain);
    await p.setDouble('scale', scale); await p.setDouble('stab', stab);
    await p.setInt('fps', fps); await p.setInt('width', width);
    await p.setBool('grid', grid); await p.setBool('ar', wantAr);
  }

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

  // ---- red ----
  void send(Map<String, dynamic> m) {
    try { widget.channel.sink.add(jsonEncode(m)); } catch (_) {}
  }
  void sendQuality() => send({'type': 'quality', 'fps': fps, 'width': width});

  void onClosed() {
    if (!mounted) return;
    Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => const ConnectPage(message: 'Se perdio la conexion con el editor.')));
  }

  void onMessage(dynamic data) {
    if (data is List<int>) {
      setState(() { frame = Uint8List.fromList(data); frames++; lastFrameAt = DateTime.now(); });
      return;
    }
    try {
      final m = jsonDecode(data as String) as Map<String, dynamic>;
      switch (m['type']) {
        case 'link':
          setState(() { editorOn = m['editor'] == true; });
        case 'state':
          final wasRec = rec;
          setState(() {
            rec = m['rec'] == true; live = m['live'] == true; playing = m['playing'] == true;
            songT = (m['t'] as num?)?.toInt() ?? 0; keys = (m['keys'] as num?)?.toInt() ?? 0;
            shot = m['shot'] as Map<String, dynamic>?;
          });
          if (wasRec != rec) HapticFeedback.heavyImpact();
        case 'recenter':
          recenter();
        case 'live':
          setState(() { live = m['on'] == true; });
      }
    } catch (_) {}
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
    q = q.mul(Quat.fromRotVec(w[0] * dt * rotGain, w[1] * dt * rotGain, w[2] * dt * rotGain));
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
      pos = Quat.yaw(-arBaseQ!.heading()).rotate(d[0] * scale, d[1] * scale, d[2] * scale);
    } else {
      target = q;
    }
    if (!hold) {
      // mando y altura: en el marco nivelado, segun hacia donde mira la camara ahora
      if (stick != Offset.zero || lift != 0) {
        final v = Quat.yaw(target.heading()).rotate(stick.dx, 0, stick.dy);
        for (var i = 0; i < 3; i++) { offset[i] += v[i] * moveSpeed * dt; }
        offset[1] += lift * moveSpeed * 0.6 * dt;
      }
      // estabilizador: suaviza el temblor sin quitar el movimiento
      final a = stab <= 0.001 ? 1.0 : 1 - math.pow(stab.clamp(0.0, 0.95), dt * 60).toDouble();
      qOut = qOut.slerpTo(target, a);
      for (var i = 0; i < 3; i++) { pOut[i] += (pos[i] + offset[i] - pOut[i]) * a; }
    }
    send({'type': 'pose', 'q': [qOut.x, qOut.y, qOut.z, qOut.w], 'p': pOut, 'zoom': zoom});
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
        // la posicion relativa tiene que dar keepP: se pone el desplazamiento en la base y el resto en el mando
        arBaseP = List.of(arP!);
      } else {
        q = keepQ;
      }
      for (var i = 0; i < 3; i++) { offset[i] = keepP[i]; }
    }
    setState(() => hold = v);
  }

  void toggleRec() {
    if (!rec) recenter();
    send({'type': 'rec', 'on': !rec});
    HapticFeedback.heavyImpact();
  }

  String fmt(int ms) {
    final neg = ms < 0;
    final s = ms.abs() / 1000.0;
    return '${neg ? '-' : ''}${s ~/ 60}:${(s % 60).toStringAsFixed(1).padLeft(4, '0')}';
  }

  // ---------------------------------------------------------------------------------------------------------
  @override
  Widget build(BuildContext context) {
    final sh = shot;
    final dur = (sh?['dur'] as num?)?.toInt() ?? 0, st = (sh?['t'] as num?)?.toInt() ?? 0;
    final progress = rec && dur > 0 ? ((songT - st) / dur).clamp(0.0, 1.0) : null;
    final stale = frames > 0 && DateTime.now().difference(lastFrameAt).inSeconds > 3;
    final showWhy = arWhy.isNotEmpty && !(track == Track.ar && arState == 'tracking');
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(children: [
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
                  if (frame != null) Image.memory(frame!, gaplessPlayback: true, fit: BoxFit.cover, filterQuality: FilterQuality.low),
                  if (grid) IgnorePointer(child: CustomPaint(painter: _Guides())),
                  if (frame == null || stale) _noImage(),
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
            decoration: const BoxDecoration(gradient: LinearGradient(begin: Alignment.topCenter, end: Alignment.bottomCenter, colors: [Color(0xCC000000), Color(0x00000000)])),
            child: Row(children: [
              _dot(editorOn ? Colors.greenAccent : Colors.redAccent),
              const SizedBox(width: 8),
              Flexible(
                child: Text(
                  rec ? 'GRABANDO  ·  $keys claves' : sh != null ? 'Toma ${sh['id']} · empieza en ${fmt(st)}' : 'Sin toma: al grabar se crea una en el cursor',
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(color: rec ? kRec : Colors.white, fontWeight: FontWeight.w700),
                ),
              ),
              const SizedBox(width: 12),
              Text(fmt(songT), style: const TextStyle(fontFeatures: [FontFeature.tabularFigures()], fontSize: 20, fontWeight: FontWeight.w800)),
              const SizedBox(width: 12),
              _trackChip(),
              const SizedBox(width: 6),
              _chip('x${zoom.toStringAsFixed(2)}', Icons.zoom_in),
              IconButton(onPressed: openSettings, icon: const Icon(Icons.tune, color: Colors.white)),
            ]),
          ),
        ),
        if (progress != null)
          Positioned(top: 0, left: 0, right: 0, child: LinearProgressIndicator(value: progress, minHeight: 3, color: kRec, backgroundColor: Colors.transparent)),
        if (showWhy) Positioned(top: 54, left: 0, right: 0, child: Center(child: _toast(arWhy))),
        // ---- mandos de movimiento (izquierda) ----
        Positioned(left: 18, bottom: 18, child: Row(crossAxisAlignment: CrossAxisAlignment.end, children: [
          _Stick(onChange: (o) => stick = o),
          const SizedBox(width: 14),
          _Lift(onChange: (v) => lift = v),
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
            _round(Icons.center_focus_strong, 'Recentrar', recenter),
            const SizedBox(height: 10),
            _round(live ? Icons.visibility : Icons.visibility_outlined, live ? 'En vivo' : 'Ensayo', () {
              send({'type': 'live', 'on': !live}); setState(() => live = !live);
            }, on: live),
            const SizedBox(height: 10),
            Listener(
              onPointerDown: (_) => setHold(true),
              onPointerUp: (_) => setHold(false),
              onPointerCancel: (_) => setHold(false),
              child: _round(Icons.pan_tool_alt, 'Soltar', null, on: hold),
            ),
            const SizedBox(height: 14),
            GestureDetector(
              onTap: toggleRec,
              child: Container(
                width: 88, height: 88,
                decoration: BoxDecoration(shape: BoxShape.circle, border: Border.all(color: Colors.white, width: 4), color: Colors.black26),
                child: Center(child: AnimatedContainer(
                  duration: const Duration(milliseconds: 180),
                  width: rec ? 34 : 66, height: rec ? 34 : 66,
                  decoration: BoxDecoration(color: kRec, borderRadius: BorderRadius.circular(rec ? 7 : 40)),
                )),
              ),
            ),
          ]),
        ),
      ]),
    );
  }

  Widget _noImage() => Container(
        color: Colors.black54,
        alignment: Alignment.center,
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          const Icon(Icons.videocam_off_outlined, size: 40, color: Colors.white54),
          const SizedBox(height: 8),
          Text(!editorOn ? 'El editor no esta abierto en el PC' : frames == 0 ? 'Esperando la imagen del editor...' : 'La imagen se corto',
              style: const TextStyle(fontWeight: FontWeight.w700)),
          const SizedBox(height: 4),
          const Text('Abre un proyecto en EditorSong (la pestana Camaras tiene la conexion).', style: TextStyle(color: Colors.white60, fontSize: 12)),
        ]),
      );

  Widget _dot(Color c) => Container(width: 10, height: 10, decoration: BoxDecoration(color: c, shape: BoxShape.circle, boxShadow: [BoxShadow(color: c, blurRadius: 6)]));

  Widget _trackChip() {
    final ok = track == Track.ar && arState == 'tracking';
    final label = track == Track.ar ? (ok ? 'Posicion real' : 'Buscando...') : 'Giroscopio';
    final c = track == Track.ar ? (ok ? Colors.greenAccent : Colors.amber) : Colors.white70;
    return GestureDetector(
      onTap: () => track == Track.ar ? stopAr() : startAr(),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 5),
        decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(8), border: Border.all(color: c.withValues(alpha: 0.6))),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(track == Track.ar ? Icons.directions_walk : Icons.screen_rotation_alt, size: 16, color: c),
          const SizedBox(width: 5),
          Text(label, style: TextStyle(color: c, fontWeight: FontWeight.w600)),
        ]),
      ),
    );
  }

  Widget _chip(String t, IconData i) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 5),
        decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(8)),
        child: Row(mainAxisSize: MainAxisSize.min, children: [Icon(i, size: 16), const SizedBox(width: 4), Text(t)]),
      );

  Widget _toast(String t) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
        decoration: BoxDecoration(color: const Color(0xDD3A2A00), borderRadius: BorderRadius.circular(8)),
        child: Text(t, style: const TextStyle(color: Colors.amberAccent)),
      );

  Widget _round(IconData i, String label, VoidCallback? f, {bool on = false}) => Column(mainAxisSize: MainAxisSize.min, children: [
        Material(
          color: on ? kAccent : Colors.white24, shape: const CircleBorder(),
          child: InkWell(customBorder: const CircleBorder(), onTap: f, child: Padding(padding: const EdgeInsets.all(11), child: Icon(i, color: on ? Colors.black : Colors.white))),
        ),
        const SizedBox(height: 2),
        Text(label, style: const TextStyle(fontSize: 10, color: Colors.white70)),
      ]);

  void openSettings() {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      backgroundColor: const Color(0xF016181D),
      builder: (_) => StatefulBuilder(builder: (ctx, set) {
        Widget slider(String label, String hint, double v, double min, double max, ValueChanged<double> f, {String Function(double)? fmtv}) => Padding(
              padding: const EdgeInsets.only(bottom: 4),
              child: Row(children: [
                SizedBox(width: 240, child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text('$label: ${fmtv != null ? fmtv(v) : v.toStringAsFixed(2)}', style: const TextStyle(fontWeight: FontWeight.w600)),
                  Text(hint, style: const TextStyle(fontSize: 11, color: Colors.white54)),
                ])),
                Expanded(child: Slider(value: v.clamp(min, max), min: min, max: max, onChanged: (x) { set(() => f(x)); setState(() {}); }, onChangeEnd: (_) => savePrefs())),
              ]),
            );
        return SafeArea(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(20, 12, 20, 12),
            child: SingleChildScrollView(child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              const Text('Ajustes de la camara', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w800)),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('Seguir mi posicion real (ARCore)'),
                subtitle: const Text('Caminar, agacharte o subir mueve la camara. Apagado: solo giroscopio y mando.'),
                value: wantAr,
                onChanged: (v) { set(() => wantAr = v); savePrefs(); v ? startAr() : stopAr(); },
              ),
              slider('Escala del movimiento', '1 m que te mueves = X m en el escenario', scale, 0.2, 5, (v) => scale = v, fmtv: (v) => '${v.toStringAsFixed(1)} m'),
              slider('Estabilizador', '0 = pulso de la mano tal cual; alto = steadicam', stab, 0, 0.9, (v) => stab = v),
              slider('Sensibilidad del giro', 'solo en modo giroscopio', rotGain, 0.2, 2, (v) => rotGain = v),
              slider('Velocidad del mando', 'metros por segundo', moveSpeed, 0.2, 6, (v) => moveSpeed = v),
              slider('Imagenes por segundo', 'mas = mas fluido, mas wifi', fps.toDouble(), 4, 30, (v) { fps = v.round(); sendQuality(); }, fmtv: (v) => '${v.round()}'),
              slider('Resolucion de la imagen', 'ancho en pixeles', width.toDouble(), 320, 1280, (v) { width = (v / 160).round() * 160; sendQuality(); }, fmtv: (v) => '${(v / 160).round() * 160} px'),
              SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Guias de tercios'), value: grid, onChanged: (v) { set(() => grid = v); setState(() {}); savePrefs(); }),
              const SizedBox(height: 6),
              OutlinedButton.icon(
                onPressed: () { Navigator.of(ctx).pop(); widget.channel.sink.close(); },
                icon: const Icon(Icons.link_off), label: const Text('Desconectar'),
              ),
            ])),
          ),
        );
      }),
    );
  }
}

// guias de tercios y centro
class _Guides extends CustomPainter {
  @override
  void paint(Canvas c, Size s) {
    final p = Paint()..color = const Color(0x55FFFFFF)..strokeWidth = 1;
    for (var i = 1; i < 3; i++) {
      c.drawLine(Offset(s.width * i / 3, 0), Offset(s.width * i / 3, s.height), p);
      c.drawLine(Offset(0, s.height * i / 3), Offset(s.width, s.height * i / 3), p);
    }
    final cx = s.width / 2, cy = s.height / 2;
    final pc = Paint()..color = const Color(0x99FFFFFF)..strokeWidth = 1.5;
    c.drawLine(Offset(cx - 10, cy), Offset(cx + 10, cy), pc);
    c.drawLine(Offset(cx, cy - 10), Offset(cx, cy + 10), pc);
  }
  @override
  bool shouldRepaint(covariant CustomPainter old) => false;
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
  static const r = 62.0;
  void upd(Offset local) {
    var d = local - const Offset(r, r);
    if (d.distance > r) d = d / d.distance * r;
    setState(() => k = d);
    final n = d / r;
    widget.onChange(n * n.distance);   // respuesta suave cerca del centro
  }
  @override
  Widget build(BuildContext context) => GestureDetector(
        onPanStart: (d) => upd(d.localPosition),
        onPanUpdate: (d) => upd(d.localPosition),
        onPanEnd: (_) { setState(() => k = Offset.zero); widget.onChange(Offset.zero); },
        child: Container(
          width: r * 2, height: r * 2,
          decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.black38, border: Border.all(color: Colors.white30, width: 2)),
          child: Stack(alignment: Alignment.center, children: [
            const Icon(Icons.open_with, color: Colors.white24, size: 40),
            Transform.translate(offset: k, child: Container(width: 50, height: 50,
                decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.white.withValues(alpha: 0.7), boxShadow: const [BoxShadow(color: Colors.black54, blurRadius: 6)]))),
          ]),
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
  static const h = 124.0;
  void upd(double y) {
    final x = ((h / 2 - y) / (h / 2)).clamp(-1.0, 1.0);
    setState(() => v = x);
    widget.onChange(x * x.abs());
  }
  @override
  Widget build(BuildContext context) => GestureDetector(
        onVerticalDragStart: (d) => upd(d.localPosition.dy),
        onVerticalDragUpdate: (d) => upd(d.localPosition.dy),
        onVerticalDragEnd: (_) { setState(() => v = 0); widget.onChange(0); },
        child: Container(
          width: 46, height: h,
          decoration: BoxDecoration(color: Colors.black38, borderRadius: BorderRadius.circular(23), border: Border.all(color: Colors.white30, width: 2)),
          child: Stack(children: [
            const Center(child: Icon(Icons.height, color: Colors.white24)),
            Positioned(left: 3, top: h / 2 - 19 - v * (h / 2 - 21), child: Container(width: 36, height: 36,
                decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.white.withValues(alpha: 0.7)))),
          ]),
        ),
      );
}
