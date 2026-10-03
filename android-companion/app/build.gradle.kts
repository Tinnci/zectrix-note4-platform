plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "dev.note4.companion"
    compileSdk = 37

    defaultConfig {
        applicationId = "dev.note4.companion"
        minSdk = 26
        targetSdk = 37
        versionCode = 2
        versionName = "2.0.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildFeatures {
        compose = true
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.material3:material3:1.5.0-alpha26")
    implementation("androidx.exifinterface:exifinterface:1.4.2")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test:core:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}

val companionFixtures = rootProject.layout.buildDirectory.dir("companion-fixtures")
val buildCompanionFixtures = tasks.register<Exec>("buildCompanionFixtures") {
    val repository = rootProject.projectDir.parentFile
    inputs.files(fileTree("${repository}/components/note4_companion"),
        fileTree("${repository}/components/note4_connectivity"),
        fileTree("${repository}/components/note4_storage"), fileTree("${repository}/tools/host_include"))
    inputs.files("${repository}/tools/build-companion-fixtures.sh",
        "${repository}/tools/companion_peer_host.cc", "${repository}/tools/book_web_host.cc")
    outputs.dir(companionFixtures)
    commandLine("bash", "${repository}/tools/build-companion-fixtures.sh", companionFixtures.get().asFile.path)
}
tasks.withType<Test>().configureEach {
    dependsOn(buildCompanionFixtures)
    inputs.dir(companionFixtures)
    systemProperty("note4.peer", companionFixtures.get().file("companion-peer-host").asFile.path)
    systemProperty("note4.web", companionFixtures.get().file("book-web-host").asFile.path)
}
