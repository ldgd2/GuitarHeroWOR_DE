package com.ghwor.editorsong.vcam_app

import android.Manifest
import android.content.pm.PackageManager
import android.os.Handler
import android.os.Looper
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

// Puente con Flutter. La logica vive en:
//   ArPoseTracker  camara virtual (ARCore, posicion real del telefono)
//   CaptureEngine  captura de cuerpo / cabeza y rostro / rostro (CameraX + MediaPipe)
//
//   "vcam/ar"            start -> "ok" | "permiso" | "instalar" | "no_soportado" | "error:<texto>",  stop
//   "vcam/ar/pose"       eventos de ArPoseTracker
//   "vcam/capture"       start {mode, front, precise, smooth, gpu} -> "ok:<sesion>" | "permiso" | "error:<texto>",  stop {session}
//   "vcam/capture/data"  eventos de CaptureEngine
class MainActivity : FlutterActivity() {
    private val main = Handler(Looper.getMainLooper())
    private var arSink: EventChannel.EventSink? = null
    private var capSink: EventChannel.EventSink? = null
    private val ar by lazy { ArPoseTracker(this) { arSink?.success(it) } }
    private val cap by lazy { CaptureEngine(this) { capSink?.success(it) } }
    private var afterPermission: ((Boolean) -> Unit)? = null

    override fun configureFlutterEngine(engine: FlutterEngine) {
        super.configureFlutterEngine(engine)
        val m = engine.dartExecutor.binaryMessenger
        MethodChannel(m, "vcam/ar").setMethodCallHandler { call, result ->
            when (call.method) {
                "start" -> withCamera { ok -> if (!ok) result.success("permiso") else { cap.stop(); startAr(result) } }
                "stop" -> { ar.stop(); result.success("ok") }
                else -> result.notImplemented()
            }
        }
        MethodChannel(m, "vcam/capture").setMethodCallHandler { call, result ->
            when (call.method) {
                "start" -> withCamera { ok ->
                    if (!ok) { result.success("permiso"); return@withCamera }
                    ar.stop()   // una sola camara a la vez
                    result.success(cap.start(call.argument<String>("mode") ?: "face", call.argument<Boolean>("front") ?: true,
                        call.argument<Boolean>("precise") ?: false, call.argument<Double>("smooth") ?: 0.5,
                        call.argument<Boolean>("gpu") ?: false, call.argument<Boolean>("hands") ?: true))
                }
                "stop" -> { cap.stop(call.argument<Int>("session") ?: -1); result.success("ok") }
                else -> result.notImplemented()
            }
        }
        EventChannel(m, "vcam/ar/pose").setStreamHandler(sink { arSink = it })
        EventChannel(m, "vcam/capture/data").setStreamHandler(sink { capSink = it })
    }

    private fun sink(set: (EventChannel.EventSink?) -> Unit) = object : EventChannel.StreamHandler {
        override fun onListen(args: Any?, events: EventChannel.EventSink?) = set(events)
        override fun onCancel(args: Any?) = set(null)
    }

    // ARCore puede tardar un momento en saber si el telefono es compatible
    private fun startAr(result: MethodChannel.Result, tries: Int = 0) {
        val r = ar.start()
        if (r == "reintentar" && tries < 20) main.postDelayed({ startAr(result, tries + 1) }, 300) else result.success(r)
    }

    private fun withCamera(f: (Boolean) -> Unit) {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) { f(true); return }
        afterPermission = f
        ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA), 7)
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, res: IntArray) {
        super.onRequestPermissionsResult(code, perms, res)
        val f = afterPermission ?: return
        afterPermission = null
        f(res.isNotEmpty() && res[0] == PackageManager.PERMISSION_GRANTED)
    }

    override fun onPause() {
        super.onPause()
        if (ar.running) { ar.stop(); arSink?.success(mapOf("state" to "stopped", "why" to "app en segundo plano")) }
        cap.pause()   // suelta la camara; al volver se reanuda sola
    }

    override fun onResume() {
        super.onResume()
        cap.resume()
    }

    override fun onDestroy() { ar.stop(); cap.stop(); super.onDestroy() }
}
