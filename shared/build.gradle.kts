plugins {
    kotlin("multiplatform")
    id("com.android.library")
}

kotlin {
    jvmToolchain(17)
    jvm()
    androidTarget()
    iosSimulatorArm64 {
        val iosBuildDir = rootProject.layout.projectDirectory.dir("build/ios-simulator-debug")
        compilations.getByName("main") {
            cinterops {
                val localAcrIosBridge by creating {
                    defFile(project.file("src/nativeInterop/cinterop/localAcrIosBridge.def"))
                    compilerOpts(
                        "-I${rootProject.projectDir}/native/core/include",
                        "-I${rootProject.projectDir}/native/ios/include",
                    )
                }
            }
        }
        binaries.framework {
            baseName = "LocalAcrShared"
            linkerOpts(
                "-L${iosBuildDir.dir("native/ios").asFile.absolutePath}",
                "-L${iosBuildDir.dir("native/core").asFile.absolutePath}",
                "-llocal_acr_ios_bridge",
                "-llocal_acr_core",
                "-llacr_kissfft",
                "-llacr_speexdsp",
                "-llacr_sqlite",
                "-framework",
                "AVFoundation",
                "-framework",
                "Foundation",
            )
        }
    }

    sourceSets {
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
            }
        }
    }
}

android {
    namespace = "com.localacr.shared"
    compileSdk = 34

    defaultConfig {
        minSdk = 26
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
