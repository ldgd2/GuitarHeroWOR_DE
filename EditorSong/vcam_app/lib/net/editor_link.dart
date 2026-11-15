import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

// Conexion con el puente del editor (EditorSong/editor/server/vcam.py): ws://IP:PUERTO/phone?pin=PIN
// Guarda el ultimo estado que manda el editor y lo comparte entre las pantallas (camara y captura).
//   editor -> movil  {type:'link', editor}  {type:'state', rec, live, playing, t, keys, shot, capRec}
//                    {type:'recenter'}  {type:'live', on}   + fotogramas JPEG (binario) del monitor
class EditorLink extends ChangeNotifier {
  EditorLink(this._ch) {
    _sub = _ch.stream.listen(_onData, onDone: _onClosed, onError: (_) => _onClosed());
  }

  final WebSocketChannel _ch;
  StreamSubscription? _sub;
  final _events = StreamController<Map<String, dynamic>>.broadcast();

  bool editorOn = false, rec = false, live = false, playing = false, capRec = false, closed = false;
  int songT = 0, keys = 0, frames = 0;
  Map<String, dynamic>? shot;
  Uint8List? frame;
  DateTime lastFrameAt = DateTime.fromMillisecondsSinceEpoch(0);

  /// mensajes sueltos del editor (recenter, live...)
  Stream<Map<String, dynamic>> get events => _events.stream;

  void send(Map<String, dynamic> m) {
    if (closed) return;
    try { _ch.sink.add(jsonEncode(m)); } catch (_) {}
  }

  void _onData(dynamic data) {
    if (data is List<int>) {
      frame = Uint8List.fromList(data);
      frames++;
      lastFrameAt = DateTime.now();
      notifyListeners();
      return;
    }
    try {
      final m = jsonDecode(data as String) as Map<String, dynamic>;
      switch (m['type']) {
        case 'link':
          editorOn = m['editor'] == true;
          notifyListeners();
        case 'state':
          rec = m['rec'] == true; live = m['live'] == true; playing = m['playing'] == true; capRec = m['capRec'] == true;
          songT = (m['t'] as num?)?.toInt() ?? 0; keys = (m['keys'] as num?)?.toInt() ?? 0;
          shot = m['shot'] as Map<String, dynamic>?;
          notifyListeners();
        case 'live':
          live = m['on'] == true;
          notifyListeners();
          _events.add(m);
        default:
          _events.add(m);
      }
    } catch (_) {}
  }

  void _onClosed() {
    if (closed) return;
    closed = true;
    notifyListeners();
  }

  Future<void> close() async {
    try { await _ch.sink.close(); } catch (_) {}
    _onClosed();
  }

  @override
  void dispose() {
    _sub?.cancel();
    _events.close();
    try { _ch.sink.close(); } catch (_) {}
    super.dispose();
  }
}
