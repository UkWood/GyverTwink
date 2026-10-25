plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "ru.ukwood.gyvertwink"
    compileSdk = 35

    defaultConfig {
        applicationId = "ru.ukwood.gyvertwink"
        minSdk = 24
        // 34, а не 35: с 35 система принудительно включает «от края до края», а оболочка рассчитана на обычные окна
        targetSdk = 34
        versionCode = 2
        versionName = "2.0.0"
    }

    // Один ключ для отладочной и релизной сборок: APK из CI ставится поверх релиза и наоборот.
    // По умолчанию публичный ключ проекта (keystore/README.md); свой задают переменные GT_KEYSTORE_*.
    signingConfigs {
        create("gt") {
            storeFile = file(System.getenv("GT_KEYSTORE_PATH") ?: "../keystore/gyvertwink.jks")
            storePassword = System.getenv("GT_KEYSTORE_PASSWORD") ?: "gyvertwink"
            keyAlias = System.getenv("GT_KEY_ALIAS") ?: "gyvertwink"
            keyPassword = System.getenv("GT_KEY_PASSWORD") ?: "gyvertwink"
        }
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("gt")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("gt")
        }
    }

    lint {
        checkReleaseBuilds = false
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(project(":core"))
}
