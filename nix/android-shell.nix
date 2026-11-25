# Dev shell for the Android app (mobile/): JDK, Android SDK (platform + build-tools
# matching mobile/build.gradle.kts) and adb. Gradle itself comes from ./gradlew.
#
#   nix develop .#android -c just build-mobile
#   nix develop .#android-emulator -c just emulate   # adds the emulator and an x86_64 image
{ lib, mkShell, androidenv, jdk21, android-tools, just, withEmulator ? false }:

let
  # AGP 9.x asks for build-tools 36.0.0 by default; keep the newest one around for aapt2.
  buildToolsVersion = "37.0.0";
  emulatorApi = "36";
  sdk = (androidenv.composeAndroidPackages ({
    platformVersions = [ "37" ];
    buildToolsVersions = [ "36.0.0" buildToolsVersion ];
    includeEmulator = withEmulator;
    includeSystemImages = withEmulator;
    systemImageTypes = [ "google_apis" ];
    abiVersions = [ "x86_64" ];
    includeNDK = false;
  } // lib.optionalAttrs withEmulator {
    platformVersions = [ emulatorApi "37" ];
  })).androidsdk;
  sdkRoot = "${sdk}/libexec/android-sdk";
in
mkShell {
  packages = [ jdk21 sdk android-tools just ];
  ANDROID_HOME = sdkRoot;
  ANDROID_SDK_ROOT = sdkRoot;
  JAVA_HOME = jdk21.home;
  SLONNECT_EMULATOR_API = emulatorApi;
  # AGP fetches a prebuilt aapt2 from Maven, which can't run on NixOS (no FHS loader).
  # Point it at the SDK's patched copy instead.
  GRADLE_OPTS = "-Dorg.gradle.project.android.aapt2FromMavenOverride=${sdkRoot}/build-tools/${buildToolsVersion}/aapt2";
}
