import 'package:flutter/services.dart';

// Captura de cuerpo / cabeza y rostro / rostro (codigo nativo: CaptureEngine.kt, CameraX + MediaPipe)
class CaptureEngine {
  static const _m = MethodChannel('vcam/capture');
  static const _e = EventChannel('vcam/capture/data');
  static Future<String> start({required String mode, required bool front, required bool precise, required double smooth, bool gpu = false, bool hands = true}) async {
    try {
      return (await _m.invokeMethod<String>('start', {'mode': mode, 'front': front, 'precise': precise, 'smooth': smooth, 'gpu': gpu, 'hands': hands})) ?? 'error';
    } catch (e) {
      return 'error:$e';
    }
  }
  /// session: la que devolvio start (`ok:N`); asi una pantalla que se cierra no para la de la siguiente
  static Future<void> stop([int session = -1]) async { try { await _m.invokeMethod('stop', {'session': session}); } catch (_) {} }
  static Stream<Map> data() => _e.receiveBroadcastStream().map((e) => e as Map);
}
