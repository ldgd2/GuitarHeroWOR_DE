// Vista previa 3D: un personaje de Warriors of Rock que sigue la captura en tiempo real.
// La dibuja una pagina local (assets/preview: three.js + el mismo rig que el editor, ver web/preview/main.js) en un
// WebView. El personaje (assets/preview/character.glb) sale de tu copia del juego y no esta en git: se genera con
//   python -m editor.band.phone_model        (desde EditorSong, con el editor habiendo montado la banda alguna vez)
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:webview_flutter/webview_flutter.dart';

import '../app/theme.dart';

class PreviewController {
  WebViewController? _web;
  bool loaded = false;
  bool _busy = false;
  String? _pending;

  // un fotograma a la vez: si llega otro mientras el anterior se procesa, solo queda el ultimo
  void _run(String js) {
    if (_web == null || !loaded) return;
    if (_busy) { _pending = js; return; }
    _busy = true;
    _web!.runJavaScript(js).whenComplete(() {
      _busy = false;
      final p = _pending; _pending = null;
      if (p != null) _run(p);
    });
  }

  void mode(String m) { if (loaded) _web?.runJavaScript('PV.mode(${jsonEncode(m)})'); }
  void face(Map<String, double> bs, List? q) => _run('PV.face(${jsonEncode(bs)},${q == null ? 'null' : jsonEncode(q)})');
  void body(List lm, [Map? hands]) => _run('PV.body(${jsonEncode(lm)}, ${jsonEncode(hands)})');
  void lost() { if (loaded) _web?.runJavaScript('PV.lost()'); }
  void neutral() { if (loaded) _web?.runJavaScript('PV.neutral()'); }
}

class ModelPreview extends StatefulWidget {
  const ModelPreview({super.key, required this.controller, required this.mode});
  final PreviewController controller;
  final String mode;
  @override
  State<ModelPreview> createState() => _ModelPreviewState();
}

class _ModelPreviewState extends State<ModelPreview> {
  late final WebViewController web;
  String? error;
  bool loading = true;

  @override
  void initState() {
    super.initState();
    web = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setBackgroundColor(kBg)
      ..addJavaScriptChannel('Preview', onMessageReceived: (m) => onMsg(m.message))
      ..loadFlutterAsset('assets/preview/index.html');
    widget.controller._web = web;
  }

  @override
  void didUpdateWidget(ModelPreview old) {
    super.didUpdateWidget(old);
    if (old.mode != widget.mode) widget.controller.mode(widget.mode);
  }

  @override
  void dispose() {
    widget.controller._web = null;
    widget.controller.loaded = false;
    super.dispose();
  }

  Future<void> onMsg(String raw) async {
    final m = jsonDecode(raw) as Map;
    if (m['ready'] == true) {
      await sendModel();
    } else if (m['loaded'] == true) {
      widget.controller.loaded = true;
      await web.runJavaScript('PV.mode(${jsonEncode(widget.mode)})');
      if (mounted) setState(() => loading = false);
    } else if (m['error'] != null) {
      if (mounted) setState(() { error = 'No se pudo cargar el personaje: ${m['error']}'; loading = false; });
    }
  }

  Future<void> sendModel() async {
    ByteData data;
    try {
      data = await rootBundle.load('assets/preview/character.glb');
    } catch (_) {
      if (mounted) {
        setState(() {
          loading = false;
          error = 'Esta version de la app no lleva el personaje. En el PC, desde EditorSong:\n'
              'python -m editor.band.phone_model\ny vuelve a instalar la app.';
        });
      }
      return;
    }
    final b64 = base64Encode(data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes));
    const step = 256 * 1024;
    for (var i = 0; i < b64.length; i += step) {
      await web.runJavaScript('PV.chunk("${b64.substring(i, i + step > b64.length ? b64.length : i + step)}")');
    }
    await web.runJavaScript('PV.finish()');
  }

  @override
  Widget build(BuildContext context) {
    return Stack(children: [
      Positioned.fill(child: WebViewWidget(
        controller: web,
        // arrastrar con un dedo gira la vista alrededor del personaje
        gestureRecognizers: {Factory<OneSequenceGestureRecognizer>(() => EagerGestureRecognizer())},
      )),
      if (loading) const Center(child: SizedBox(width: 28, height: 28, child: CircularProgressIndicator(strokeWidth: 2.5))),
      if (error != null) Center(child: Padding(
        padding: const EdgeInsets.all(24),
        child: Text(error!, textAlign: TextAlign.center, style: const TextStyle(color: kMuted, fontSize: 13)),
      )),
    ]);
  }
}
