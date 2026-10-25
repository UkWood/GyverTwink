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
        versionCode = 1
        versionName = "2.0.0-alpha1"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
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
