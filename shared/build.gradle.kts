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
    ndkVersion = "27.2.12479018"

    defaultConfig {
        minSdk = 26
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DLACR_BUILD_TESTS=OFF",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                )
                targets += "local_acr_jni"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
