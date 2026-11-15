package com.ghwor.editorsong.vcam_app

import android.Manifest
import android.content.pm.PackageManager
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.os.Looper
import android.view.Surface
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.google.ar.core.ArCoreApk
import com.google.ar.core.CameraConfig
import com.google.ar.core.CameraConfigFilter
import com.google.ar.core.Config
import com.google.ar.core.Session
import com.google.ar.core.TrackingFailureReason
import com.google.ar.core.TrackingState
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel
import java.util.EnumSet

// Seguimiento de 6 grados de libertad con ARCore: posicion real del telefono en metros (caminar, agacharse, subir)
// y su orientacion, sin mostrar la imagen de la camara. ARCore necesita una textura de GL para la camara, asi que se
// crea un contexto EGL propio, sin ventana, en un hilo aparte.
//
//   canal "vcam/ar"       start -> "ok" | "permiso" | "instalar" | "no_soportado" | "error:<texto>",  stop
//   canal "vcam/ar/pose"  {p:[x,y,z], q:[x,y,z,w], state:"tracking"|"paused"|"stopped", why:"..."}
//                         mundo de ARCore: Y arriba (gravedad), metros. Pose orientada a la pantalla (mira a -Z).
class MainActivity : FlutterActivity() {
    private var session: Session? = null
    private var thread: Thread? = null
    @Volatile private var running = false
    private var sink: EventChannel.EventSink? = null
    private val main = Handler(Looper.getMainLooper())
    private var pendingStart: MethodChannel.Result? = null
    private var installAsked = false

    override fun configureFlutterEngine(engine: FlutterEngine) {
        super.configureFlutterEngine(engine)
        MethodChannel(engine.dartExecutor.binaryMessenger, "vcam/ar").setMethodCallHandler { call, result ->
            when (call.method) {
                "start" -> start(result)
                "stop" -> { stop(); result.success("ok") }
                else -> result.notImplemented()
            }
        }
        EventChannel(engine.dartExecutor.binaryMessenger, "vcam/ar/pose").setStreamHandler(object : EventChannel.StreamHandler {
            override fun onListen(args: Any?, events: EventChannel.EventSink?) { sink = events }
            override fun onCancel(args: Any?) { sink = null }
        })
    }

    private fun start(result: MethodChannel.Result) {
        if (running) { result.success("ok"); return }
        val avail = ArCoreApk.getInstance().checkAvailability(this)
        if (avail.isTransient) { main.postDelayed({ start(result) }, 300); return }
        if (!avail.isSupported) { result.success("no_soportado"); return }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            pendingStart = result
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA), 7)
            return
        }
        try {
            when (ArCoreApk.getInstance().requestInstall(this, !installAsked)) {
                ArCoreApk.InstallStatus.INSTALL_REQUESTED -> { installAsked = true; result.success("instalar"); return }
                else -> {}
            }
            val s = Session(this)
            // 60 imagenes/s si el telefono lo permite (pose mas fluida)
            val filter = CameraConfigFilter(s).setTargetFps(EnumSet.of(CameraConfig.TargetFps.TARGET_FPS_60))
            s.getSupportedCameraConfigs(filter).firstOrNull()?.let { s.cameraConfig = it }
            val cfg = Config(s)
            cfg.planeFindingMode = Config.PlaneFindingMode.DISABLED
            cfg.lightEstimationMode = Config.LightEstimationMode.DISABLED
            cfg.depthMode = Config.DepthMode.DISABLED
            cfg.updateMode = Config.UpdateMode.BLOCKING
            cfg.focusMode = Config.FocusMode.AUTO
            s.configure(cfg)
            session = s
            running = true
            thread = Thread({ loop(s) }, "vcam-arcore").also { it.start() }
            result.success("ok")
        } catch (e: Exception) {
            result.success("error:" + (e.message ?: e.javaClass.simpleName))
        }
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, res: IntArray) {
        super.onRequestPermissionsResult(code, perms, res)
        val r = pendingStart ?: return
        pendingStart = null
        if (res.isNotEmpty() && res[0] == PackageManager.PERMISSION_GRANTED) start(r) else r.success("permiso")
    }

    private fun loop(s: Session) {
        // contexto GL sin ventana (pbuffer 1x1) + textura externa para la imagen de la camara
        val dpy: EGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val ver = IntArray(2); EGL14.eglInitialize(dpy, ver, 0, ver, 1)
        val attrs = intArrayOf(EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_NONE)
        val cfgs = arrayOfNulls<EGLConfig>(1); val num = IntArray(1)
        EGL14.eglChooseConfig(dpy, attrs, 0, cfgs, 0, 1, num, 0)
        val ctx: EGLContext = EGL14.eglCreateContext(dpy, cfgs[0], EGL14.EGL_NO_CONTEXT, intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
        val surf: EGLSurface = EGL14.eglCreatePbufferSurface(dpy, cfgs[0], intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0)
        EGL14.eglMakeCurrent(dpy, surf, surf, ctx)
        val tex = IntArray(1); GLES20.glGenTextures(1, tex, 0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, tex[0])
        s.setCameraTextureName(tex[0])
        try {
            @Suppress("DEPRECATION")
            val rot = windowManager.defaultDisplay.rotation
            val m = resources.displayMetrics
            s.setDisplayGeometry(rot, m.widthPixels, m.heightPixels)
            s.resume()
            var lastRot = rot
            while (running) {
                @Suppress("DEPRECATION")
                val r = windowManager.defaultDisplay.rotation
                if (r != lastRot) { s.setDisplayGeometry(r, m.widthPixels, m.heightPixels); lastRot = r }
                val frame = try { s.update() } catch (e: Exception) { Thread.sleep(10); continue }
                val cam = frame.camera
                val pose = cam.displayOrientedPose
                val state = when (cam.trackingState) { TrackingState.TRACKING -> "tracking"; TrackingState.PAUSED -> "paused"; else -> "stopped" }
                val why = when (cam.trackingFailureReason) {
                    TrackingFailureReason.INSUFFICIENT_LIGHT -> "poca luz"
                    TrackingFailureReason.EXCESSIVE_MOTION -> "movimiento muy brusco"
                    TrackingFailureReason.INSUFFICIENT_FEATURES -> "apunta a algo con mas detalle (no a una pared lisa)"
                    TrackingFailureReason.CAMERA_UNAVAILABLE -> "camara ocupada por otra app"
                    TrackingFailureReason.BAD_STATE -> "reiniciando"
                    else -> ""
                }
                val msg = mapOf(
                    "p" to listOf(pose.tx().toDouble(), pose.ty().toDouble(), pose.tz().toDouble()),
                    "q" to listOf(pose.qx().toDouble(), pose.qy().toDouble(), pose.qz().toDouble(), pose.qw().toDouble()),
                    "state" to state, "why" to why)
                main.post { sink?.success(msg) }
            }
        } catch (e: Exception) {
            main.post { sink?.success(mapOf("state" to "stopped", "why" to (e.message ?: "error de ARCore"))) }
        } finally {
            try { s.pause() } catch (_: Exception) {}
            s.close()
            EGL14.eglMakeCurrent(dpy, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            EGL14.eglDestroySurface(dpy, surf); EGL14.eglDestroyContext(dpy, ctx); EGL14.eglTerminate(dpy)
        }
    }

    private fun stop() {
        running = false
        thread?.join(1500)
        thread = null
        session = null
    }

    override fun onPause() { super.onPause(); if (running) { stop(); main.post { sink?.success(mapOf("state" to "stopped", "why" to "app en segundo plano")) } } }
    override fun onDestroy() { stop(); super.onDestroy() }
}
