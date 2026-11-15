import java.net.URI

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "com.ghwor.editorsong.vcam_app"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = "com.ghwor.editorsong.vcam_app"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = maxOf(24, flutter.minSdkVersion)   // ARCore
        targetSdk = flutter.targetSdkVersion
        // Uses the version code from pubspec.yaml. When using split APKs, 1000 * ABI_VERSION
        // is added automatically by Flutter. (https://developer.android.com/studio/build/configure-apk-splits#configure-APK-versions)
        // You can force using the value of versionCode by specifying the `-P force-version-code-ignoring-abi=true`
        // flag during build.
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    // MediaPipe lee los modelos .task directamente del APK: no se pueden comprimir
    androidResources {
        noCompress += listOf("task")
    }

    // Android 15+ con paginas de 16 KB: librerias nativas sin comprimir y alineadas dentro del APK
    // (CameraX >= 1.4 y MediaPipe >= 0.10.35 ya traen sus .so con segmentos LOAD alineados a 16 KB)
    packaging {
        jniLibs { useLegacyPackaging = false }
    }

    buildTypes {
        release {
            // TODO: Add your own signing config for the release build.
            // Signing with the debug keys for now, so `flutter run --release` works.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}

dependencies {
    implementation("com.google.ar:core:1.57.0")
    // captura de cuerpo y cara: camara + MediaPipe Tasks (en el telefono, sin red)
    implementation("androidx.camera:camera-core:1.6.2")
    implementation("androidx.camera:camera-camera2:1.6.2")
    implementation("androidx.camera:camera-lifecycle:1.6.2")
    implementation("com.google.mediapipe:tasks-vision:0.10.35")
    implementation("androidx.core:core-ktx:1.13.1")
}

// Modelos de MediaPipe (publicos de Google, licencia Apache 2.0): no se guardan en git; se descargan la primera vez
val mediapipeModels = mapOf(
    "face_landmarker.task" to "face_landmarker/face_landmarker/float16/1/face_landmarker.task",
    "pose_landmarker_full.task" to "pose_landmarker/pose_landmarker_full/float16/1/pose_landmarker_full.task",
    "pose_landmarker_heavy.task" to "pose_landmarker/pose_landmarker_heavy/float16/1/pose_landmarker_heavy.task",
    "hand_landmarker.task" to "hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
)
val downloadModels by tasks.registering {
    val dir = file("src/main/assets")
    // se ejecuta solo si falta alguno (sin depender de la cache de Gradle)
    outputs.upToDateWhen { mediapipeModels.keys.all { File(dir, it).let { f -> f.exists() && f.length() > 0 } } }
    doLast {
        dir.mkdirs()
        for ((name, path) in mediapipeModels) {
            val f = File(dir, name)
            if (f.exists() && f.length() > 0) continue
            logger.lifecycle("Descargando el modelo $name")
            URI("https://storage.googleapis.com/mediapipe-models/$path").toURL().openStream().use { input ->
                f.outputStream().use { input.copyTo(it) }
            }
        }
    }
}
tasks.named("preBuild") { dependsOn(downloadModels) }
