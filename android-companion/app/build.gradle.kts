plugins {
    id("com.android.application")
}

android {
    namespace = "dev.zectrix.note4.companion"
    compileSdk = 37

    defaultConfig {
        applicationId = "dev.zectrix.note4.companion"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildFeatures {
        viewBinding = false
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}
