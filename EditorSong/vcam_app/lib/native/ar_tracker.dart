import 'package:flutter/services.dart';

// Camara virtual con ARCore (codigo nativo: ArPoseTracker.kt)
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
