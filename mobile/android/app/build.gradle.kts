plugins {
    id("com.android.application")
}
val gameVersion = providers.gradleProperty("gtdVersion")
    .orElse(providers.environmentVariable("GTD_RELEASE_VERSION"))
    .getOrElse(file("../../GameVersion.xcconfig").readText().substringAfter("GTD_GAME_VERSION = ").trim())
require(Regex("[0-9]+\\.[0-9]+\\.[0-9]+").matches(gameVersion)) { "gtdVersion must be major.minor.patch" }
android {
    namespace = "com.guessthedrawing.mobile"
    compileSdk = 36
    defaultConfig {
        applicationId = providers.gradleProperty("gtdApplicationId").getOrElse("com.guessthedrawing.mobile")
        minSdk = 26
        targetSdk = 36
        versionCode = providers.gradleProperty("gtdVersionCode").getOrElse("5").toInt()
        versionName = gameVersion
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        buildConfigField("String", "GAME_VERSION", "\"$gameVersion\"")
    }
    buildFeatures { buildConfig = true }
    // Keep every language available for the in-app selector, including offline installs from an AAB.
    bundle { language { enableSplit = false } }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets.named("main") {
        assets.directories.add("build/generated/flags")
    }
    sourceSets.named("test") {
        resources.directories.add("../../fixtures")
    }
}
kotlin { compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) } }
val syncCountryFlags = tasks.register<Sync>("syncCountryFlags") {
    from("../../../country-flags") { include("*.png") }
    into(layout.buildDirectory.dir("generated/flags/country-flags"))
}
tasks.named("preBuild").configure { dependsOn(syncCountryFlags) }
dependencies {
    implementation("androidx.activity:activity-ktx:1.10.1")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test:core:1.6.1")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
