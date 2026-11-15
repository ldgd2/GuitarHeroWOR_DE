package com.ghwor.editorsong.vcam_app

import android.graphics.Bitmap
import android.graphics.Matrix
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Size
import android.view.Surface
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.framework.image.MPImage
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.core.Delegate
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarker
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarkerResult
import com.google.mediapipe.tasks.vision.handlandmarker.HandLandmarker
import com.google.mediapipe.tasks.vision.handlandmarker.HandLandmarkerResult
import com.google.mediapipe.tasks.vision.poselandmarker.PoseLandmarker
import com.google.mediapipe.tasks.vision.poselandmarker.PoseLandmarkerResult
import io.flutter.embedding.android.FlutterActivity
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.sqrt

// Captura de interpretacion con la camara del telefono (CameraX + MediaPipe Tasks, en el telefono, sin red):
//   modo "face"  solo rostro, camara frontal a 1280x720: 52 blendshapes (boca, labios, ojos, mirada, cejas, mejillas)
//   modo "head"  cabeza y rostro: lo mismo + giro de la cabeza (matriz de transformacion facial)
//   modo "body"  cuerpo entero: 33 puntos 3D en metros (MediaPipe Pose; modelo "heavy" en precision alta)
// Todo pasa por un filtro One Euro (quita el temblor sin retrasar el gesto) y se emite ~30 veces por segundo:
//   {kind:"face", ok, bs:{nombre:0..1}, q:[x,y,z,w], pts:[x,y]*n (dibujo), rings:[...], fps}
//   {kind:"body", ok, lm:[x,y,z,visibilidad]*33, pts:[x,y,v]*33 (dibujo, normalizado), fps}
//   {kind:"status", state:"error"|"stopped", why}
class CaptureEngine(private val activity: FlutterActivity, private val emit: (Map<String, Any>) -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private var exec: ExecutorService? = null
    private var provider: ProcessCameraProvider? = null
    @Volatile private var face: FaceLandmarker? = null
    @Volatile private var pose: PoseLandmarker? = null
    // manos (modo cuerpo): dedos y agarre del micro. Va en paralelo al cuerpo con la misma imagen; el cuerpo usa
    // las ultimas manos que hayan llegado (si son recientes)
    @Volatile private var hand: HandLandmarker? = null
    @Volatile private var lastHands: HandLandmarkerResult? = null
    @Volatile private var lastHandsAt = 0L
    private var useHands = true
    private var handBank = OneEuroBank(2.0, 1.2)
    // Hasta 2 fotogramas en MediaPipe a la vez: mientras detecta uno, el siguiente ya se esta convirtiendo y girando
    // (con 1 solo se sumaban los dos tiempos y bajaba a ~7 fps). En modo en vivo MediaPipe DESCARTA fotogramas sin
    // llamar al resultado cuando va cargado: si en 400 ms no llega nada, la cuenta se pone a cero (si no, moria a 0 fps).
    private val inFlight = AtomicInteger(0)
    @Volatile private var inFlightAt = 0L
    @Volatile private var lastTs = 0L
    @Volatile private var lastResult = 0L
    private var mode = ""
    private var front = true
    private var smooth = 0.5
    private var dev = "CPU"
    @Volatile private var gen = 0        // cada start() invalida lo que quedase del anterior
    private var session = 0               // la de la pantalla que la pidio: solo esa la puede parar
    private var params: Array<Any>? = null   // para reanudar al volver a la app
    private var paused = false
    private val bank = OneEuroBank(2.0, 0.6)
    private var bodyBank = OneEuroBank(1.0, 0.8)
    private var lastQ: DoubleArray? = null
    private var frames = 0
    private var fpsT = 0L
    private var fps = 0.0
    var running = false
        private set

    // contorno, labios, ojos y cejas (malla de MediaPipe Face Mesh) para dibujar la cara en el telefono
    private val rings = listOf(
        intArrayOf(10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109),
        intArrayOf(61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291, 375, 321, 405, 314, 17, 84, 181, 91, 146),
        intArrayOf(78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308, 324, 318, 402, 317, 14, 87, 178, 88, 95),
        intArrayOf(33, 246, 161, 160, 159, 158, 157, 173, 133, 155, 154, 153, 145, 144, 163, 7),
        intArrayOf(362, 398, 384, 385, 386, 387, 388, 466, 263, 249, 390, 373, 374, 380, 381, 382),
        intArrayOf(70, 63, 105, 66, 107), intArrayOf(336, 296, 334, 293, 300),
        intArrayOf(468), intArrayOf(473))   // iris (centro)

    /** mode: face | head | body; front: camara frontal; precise: modelo pesado / mas resolucion; smooth 0..1;
     *  gpu: delegado GPU de MediaPipe (en algunas GPU Mali detecta mal sin dar error: por defecto CPU) */
    fun start(mode: String, front: Boolean, precise: Boolean, smooth: Double, gpu: Boolean = false, hands: Boolean = true): String {
        session++
        useHands = hands
        params = arrayOf(mode, front, precise, smooth, gpu)
        paused = false
        launch(mode, front, precise, smooth, gpu)
        return "ok:$session"
    }

    /** Para la sesion indicada (o la que haya, con -1). Una pantalla que se cierra no para la de la nueva. */
    fun stop(s: Int = -1) {
        if (s >= 0 && s != session) return
        params = null
        halt()
    }

    // la app pasa a segundo plano: se suelta la camara pero se recuerda para volver
    fun pause(): Boolean { if (params == null) return false; paused = true; halt(); return true }
    fun resume() {
        val p = params ?: return
        if (!paused) return
        paused = false
        launch(p[0] as String, p[1] as Boolean, p[2] as Boolean, p[3] as Double, p[4] as Boolean)
    }

    private fun launch(mode: String, front: Boolean, precise: Boolean, smooth: Double, gpu: Boolean) {
        halt()
        val my = ++gen
        frames = 0; fpsT = 0L; fps = 0.0; lastResult = 0L
        watchdog(my)
        this.mode = mode; this.front = front; this.smooth = smooth.coerceIn(0.0, 1.0)
        // suavizado: mas = corte mas bajo en reposo
        val mc = 3.5 - 2.8 * this.smooth
        bank.reset(); bodyBank = OneEuroBank(mc * 0.5, 0.8); handBank = OneEuroBank(mc * 0.8, 1.2); lastQ = null; lastHands = null
        for (i in lastLm.indices) lastLm[i] = 0.0
        val ex = Executors.newSingleThreadExecutor(); exec = ex
        // el modelo (hasta 30 MB) se carga en el hilo de la camara, no en el de la interfaz (se congelaba la app)
        ex.execute {
            try {
                if (mode == "body") {
                    pose = makePose(precise, gpu)
                    if (useHands) { val d = dev; try { hand = makeHand(gpu) } catch (_: Exception) { hand = null }; dev = d }
                } else face = makeFace(gpu)
            } catch (e: Exception) {
                status("error", "No se pudo cargar el modelo: " + (e.message ?: e.javaClass.simpleName)); return@execute
            }
            if (my != gen) { face?.close(); face = null; pose?.close(); pose = null; hand?.close(); hand = null; return@execute }
            main.post { if (my == gen) bindCamera(my, ex, mode, precise) }
        }
    }

    // cada medio segundo: fps reales y, si MediaPipe lleva un rato sin dar resultado, "no detecto" (no se queda
    // congelado el ultimo estado)
    private fun watchdog(my: Int) {
        main.postDelayed({
            if (my != gen) return@postDelayed
            val now = SystemClock.uptimeMillis()
            if (running && now - lastResult > 800) {
                fps = 0.0
                post(mapOf("kind" to (if (mode == "body") "body" else "face"), "ok" to false, "fps" to fps, "dev" to dev))
            }
            watchdog(my)
        }, 500)
    }

    private fun bindCamera(my: Int, ex: ExecutorService, mode: String, precise: Boolean) {
        // MediaPipe reduce la imagen a 128-256 px por dentro: 640x480 basta y convertirla cuesta 4 veces menos que 720p.
        // Precision alta: mas resolucion (caras lejanas, cuerpo entero a varios metros)
        val size = if (precise) Size(1280, 720) else Size(640, 480)
        val fut = ProcessCameraProvider.getInstance(activity)
        fut.addListener({
            if (my != gen) return@addListener
            try {
                val p = fut.get(); provider = p
                @Suppress("DEPRECATION")
                val rot = (if (Build.VERSION.SDK_INT >= 30) activity.display?.rotation else null) ?: activity.windowManager.defaultDisplay.rotation
                val analysis = ImageAnalysis.Builder()
                    .setResolutionSelector(ResolutionSelector.Builder()
                        .setResolutionStrategy(ResolutionStrategy(size, ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER)).build())
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                    .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
                    .setTargetRotation(rot)
                    .build()
                analysis.setAnalyzer(ex) { analyze(it) }
                val sel = if (front) CameraSelector.DEFAULT_FRONT_CAMERA else CameraSelector.DEFAULT_BACK_CAMERA
                p.unbindAll()
                p.bindToLifecycle(activity as LifecycleOwner, sel, analysis)
                running = true
            } catch (e: Exception) {
                status("error", "No se pudo abrir la camara: " + (e.message ?: e.javaClass.simpleName))
            }
        }, ContextCompat.getMainExecutor(activity))
    }

    private fun halt() {
        running = false
        gen++
        try { provider?.unbindAll() } catch (_: Exception) {}
        // los modelos se cierran en su hilo, despues del fotograma que este en curso
        val f = face; val p = pose; val h = hand; face = null; pose = null; hand = null
        exec?.let { ex -> try { ex.execute { f?.close(); p?.close(); h?.close() } } catch (_: Exception) { f?.close(); p?.close(); h?.close() }; ex.shutdown() }
        exec = null
        inFlight.set(0)
    }

    // GPU solo si se pide; si no arranca, CPU
    private fun <T> withDelegate(gpu: Boolean, build: (Boolean) -> T): T {
        if (gpu) try { return build(true).also { dev = "GPU" } } catch (_: Exception) {}
        return build(false).also { dev = "CPU" }
    }

    private fun base(model: String, gpu: Boolean) = BaseOptions.builder().setModelAssetPath(model)
        .setDelegate(if (gpu) Delegate.GPU else Delegate.CPU).build()

    private fun makeFace(gpu: Boolean): FaceLandmarker {
        fun build(gpu: Boolean) = FaceLandmarker.createFromOptions(activity, FaceLandmarker.FaceLandmarkerOptions.builder()
            .setBaseOptions(base("face_landmarker.task", gpu))
            .setRunningMode(RunningMode.LIVE_STREAM)
            .setNumFaces(1)
            .setMinFaceDetectionConfidence(0.4f).setMinFacePresenceConfidence(0.4f).setMinTrackingConfidence(0.4f)
            .setOutputFaceBlendshapes(true)
            .setOutputFacialTransformationMatrixes(true)
            .setResultListener { r: FaceLandmarkerResult, _: MPImage -> onFace(r) }
            .setErrorListener { e -> done(); status("error", e.message ?: "error de MediaPipe") }
            .build())
        return withDelegate(gpu) { build(it) }
    }

    private fun makeHand(gpu: Boolean): HandLandmarker {
        fun build(gpu: Boolean) = HandLandmarker.createFromOptions(activity, HandLandmarker.HandLandmarkerOptions.builder()
            .setBaseOptions(base("hand_landmarker.task", gpu))
            .setRunningMode(RunningMode.LIVE_STREAM)
            .setNumHands(2)
            .setMinHandDetectionConfidence(0.4f).setMinHandPresenceConfidence(0.4f).setMinTrackingConfidence(0.4f)
            .setResultListener { r: HandLandmarkerResult, _: MPImage -> lastHands = r; lastHandsAt = SystemClock.uptimeMillis() }
            .setErrorListener { _ -> }
            .build())
        return withDelegate(gpu) { build(it) }
    }

    private fun makePose(precise: Boolean, gpu: Boolean): PoseLandmarker {
        val model = if (precise) "pose_landmarker_heavy.task" else "pose_landmarker_full.task"
        fun build(gpu: Boolean) = PoseLandmarker.createFromOptions(activity, PoseLandmarker.PoseLandmarkerOptions.builder()
            .setBaseOptions(base(model, gpu))
            .setRunningMode(RunningMode.LIVE_STREAM)
            .setNumPoses(1)
            .setMinPoseDetectionConfidence(0.4f).setMinPosePresenceConfidence(0.4f).setMinTrackingConfidence(0.4f)
            .setResultListener { r: PoseLandmarkerResult, _: MPImage -> onPose(r) }
            .setErrorListener { e -> done(); status("error", e.message ?: "error de MediaPipe") }
            .build())
        return withDelegate(gpu) { build(it) }
    }

    private fun analyze(img: ImageProxy) {
        try {
            val now = SystemClock.uptimeMillis()
            if (inFlight.get() >= 2) {
                if (now - inFlightAt < 400) return   // MediaPipe sigue ocupado (y aun puede responder)
                inFlight.set(0)                                                // los descarto sin avisar
            }
            inFlight.incrementAndGet(); inFlightAt = now
            // la imagen de la camara viene en la orientacion del sensor: se pone derecha antes de detectar (como en
            // los ejemplos de MediaPipe; las caras y cuerpos de lado se detectan mucho peor)
            val src = img.toBitmap()
            val deg = img.imageInfo.rotationDegrees
            val bmp = if (deg == 0) src else Bitmap.createBitmap(src, 0, 0, src.width, src.height, Matrix().apply { postRotate(deg.toFloat()) }, false)
            val mp = BitmapImageBuilder(bmp).build()
            val t = maxOf(now, lastTs + 1); lastTs = t   // MediaPipe exige marcas de tiempo crecientes
            face?.detectAsync(mp, t) ?: pose?.detectAsync(mp, t) ?: done()
            try { hand?.detectAsync(mp, t) } catch (_: Exception) {}
        } catch (e: Exception) {
            done()
        } finally {
            img.close()
        }
    }

    private fun done() { if (inFlight.decrementAndGet() < 0) inFlight.set(0) }

    private fun tick() {
        frames++
        val now = SystemClock.uptimeMillis()
        lastResult = now
        if (fpsT == 0L) fpsT = now
        if (now - fpsT >= 1000) { fps = frames * 1000.0 / (now - fpsT); frames = 0; fpsT = now }
    }

    private fun onFace(r: FaceLandmarkerResult) {
        done(); tick()
        val t = System.nanoTime()
        val shapes = r.faceBlendshapes().orElse(null)?.firstOrNull()
        val lms = r.faceLandmarks().firstOrNull()
        if (shapes == null || lms == null) { post(mapOf("kind" to "face", "ok" to false, "fps" to fps, "dev" to dev)); return }
        val bs = HashMap<String, Double>()
        for (c in shapes) {
            val n = c.categoryName() ?: continue
            if (n == "_neutral") continue
            bs[n] = bank.filter(n, c.score().toDouble(), t).coerceIn(0.0, 1.0)
        }
        val msg = HashMap<String, Any>()
        msg["kind"] = "face"; msg["ok"] = true; msg["bs"] = bs; msg["fps"] = fps; msg["dev"] = dev
        if (mode == "head") r.facialTransformationMatrixes().orElse(null)?.firstOrNull()?.let { msg["q"] = headQuat(it, t) }
        val pts = ArrayList<Double>(); val rl = ArrayList<Int>()
        for (ring in rings) { var n = 0; for (i in ring) if (i < lms.size) { pts.add(lms[i].x().toDouble()); pts.add(lms[i].y().toDouble()); n++ }; rl.add(n) }
        msg["pts"] = pts; msg["rings"] = rl; msg["mirror"] = front
        post(msg)
    }

    // matriz 4x4 (por columnas) de la cara canonica a la camara -> cuaternion filtrado, con signo continuo
    private fun headQuat(m: FloatArray, t: Long): List<Double> {
        val r00 = m[0].toDouble(); val r10 = m[1].toDouble(); val r20 = m[2].toDouble()
        val r01 = m[4].toDouble(); val r11 = m[5].toDouble(); val r21 = m[6].toDouble()
        val r02 = m[8].toDouble(); val r12 = m[9].toDouble(); val r22 = m[10].toDouble()
        // quitar la escala de cada columna
        val sx = sqrt(r00 * r00 + r10 * r10 + r20 * r20); val sy = sqrt(r01 * r01 + r11 * r11 + r21 * r21); val sz = sqrt(r02 * r02 + r12 * r12 + r22 * r22)
        val a = r00 / sx; val b = r01 / sy; val c = r02 / sz
        val d = r10 / sx; val e = r11 / sy; val f = r12 / sz
        val g = r20 / sx; val h = r21 / sy; val k = r22 / sz
        val q = DoubleArray(4)
        val tr = a + e + k
        if (tr > 0) { val s = sqrt(tr + 1.0) * 2; q[3] = 0.25 * s; q[0] = (h - f) / s; q[1] = (c - g) / s; q[2] = (d - b) / s }
        else if (a > e && a > k) { val s = sqrt(1.0 + a - e - k) * 2; q[3] = (h - f) / s; q[0] = 0.25 * s; q[1] = (b + d) / s; q[2] = (c + g) / s }
        else if (e > k) { val s = sqrt(1.0 + e - a - k) * 2; q[3] = (c - g) / s; q[0] = (b + d) / s; q[1] = 0.25 * s; q[2] = (f + h) / s }
        else { val s = sqrt(1.0 + k - a - e) * 2; q[3] = (d - b) / s; q[0] = (c + g) / s; q[1] = (f + h) / s; q[2] = 0.25 * s }
        lastQ?.let { p -> if (p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3] < 0) for (i in 0..3) q[i] = -q[i] }
        for (i in 0..3) q[i] = bank.filter("q$i", q[i], t)
        val n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).coerceAtLeast(1e-9)
        for (i in 0..3) q[i] /= n
        lastQ = q
        return q.toList()
    }

    private val lastLm = DoubleArray(33 * 3)
    private fun onPose(r: PoseLandmarkerResult) {
        done(); tick()
        val t = System.nanoTime()
        val w = r.worldLandmarks().firstOrNull()
        val n2 = r.landmarks().firstOrNull()
        if (w == null || n2 == null || w.size < 33) { post(mapOf("kind" to "body", "ok" to false, "fps" to fps, "dev" to dev)); return }
        val lm = ArrayList<Double>(33 * 4); val pts = ArrayList<Double>(33 * 3)
        for (i in 0 until 33) {
            val vis = w[i].visibility().orElse(1f).toDouble()
            // un punto que no se ve (tapado o fuera de la imagen) se queda donde estaba en vez de saltar
            if (vis >= 0.35 || lastLm[i * 3] == 0.0) {
                lastLm[i * 3] = bodyBank.filter("x$i", w[i].x().toDouble(), t)
                lastLm[i * 3 + 1] = bodyBank.filter("y$i", w[i].y().toDouble(), t)
                lastLm[i * 3 + 2] = bodyBank.filter("z$i", w[i].z().toDouble(), t)
            }
            lm.add(lastLm[i * 3]); lm.add(lastLm[i * 3 + 1]); lm.add(lastLm[i * 3 + 2]); lm.add(vis)
            pts.add(n2[i].x().toDouble()); pts.add(n2[i].y().toDouble()); pts.add(vis)
        }
        val out = mutableMapOf<String, Any>("kind" to "body", "ok" to true, "lm" to lm, "pts" to pts, "fps" to fps, "mirror" to front, "dev" to dev)
        handsFor(n2, t)?.let { (h, hp) -> out["hands"] = h; out["hpts"] = hp }
        post(out)
    }

    // manos recientes -> lado del cantante por cercania a cada muneca del cuerpo en la imagen (la etiqueta
    // izquierda/derecha de MediaPipe depende de si la imagen esta en espejo). 21 puntos x (x, y, z) en metros.
    private fun handsFor(n2: List<com.google.mediapipe.tasks.components.containers.NormalizedLandmark>, t: Long): Pair<Map<String, List<Double>>, List<Double>>? {
        val r = lastHands ?: return null
        if (SystemClock.uptimeMillis() - lastHandsAt > 200) return null
        val img = r.landmarks(); val world = r.worldLandmarks()
        if (img.isEmpty()) return null
        val wr = mapOf("L" to n2[15], "R" to n2[16])
        val pairs = ArrayList<Triple<Double, Int, String>>()
        img.forEachIndexed { k, h -> for ((sd, w) in wr) pairs.add(Triple(Math.hypot((h[0].x() - w.x()).toDouble(), (h[0].y() - w.y()).toDouble()), k, sd)) }
        pairs.sortBy { it.first }
        val used = HashSet<Int>(); val out = HashMap<String, List<Double>>(); val pts = ArrayList<Double>()
        for ((d, k, sd) in pairs) {
            if (d > 0.2 || k in used || out.containsKey(sd) || k >= world.size) continue
            used.add(k)
            val wl = world[k]; val arr = ArrayList<Double>(63)
            for (i in 0 until 21) {
                arr.add(handBank.filter("$sd x$i", wl[i].x().toDouble(), t))
                arr.add(handBank.filter("$sd y$i", wl[i].y().toDouble(), t))
                arr.add(handBank.filter("$sd z$i", wl[i].z().toDouble(), t))
            }
            out[sd] = arr
            for (i in 0 until 21) { pts.add(img[k][i].x().toDouble()); pts.add(img[k][i].y().toDouble()) }
        }
        return if (out.isEmpty()) null else Pair(out, pts)
    }

    private fun status(state: String, why: String) = post(mapOf("kind" to "status", "state" to state, "why" to why))
    private fun post(m: Map<String, Any>) = main.post { emit(m) }
}
