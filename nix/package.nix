{ lib, stdenv, cmake, ninja, wrapQtAppsHook, qtbase, qtdeclarative, qtsvg, qtwayland, scrcpy, android-tools }:

stdenv.mkDerivation {
  pname = "slonnect";
  version = "0.1.0";

  src = lib.cleanSourceWith {
    src = ../desktop;
    filter = path: type: !(lib.hasPrefix "build" (baseNameOf path)) && baseNameOf path != "result";
  };

  nativeBuildInputs = [ cmake ninja wrapQtAppsHook ];
  buildInputs = [ qtbase qtdeclarative qtsvg qtwayland ];

  # Screen mirroring shells out to scrcpy/adb; make them available out of the box.
  qtWrapperArgs = [ "--suffix" "PATH" ":" (lib.makeBinPath [ scrcpy android-tools ]) ];

  meta = {
    description = "Material Design desktop client for KDE Connect: SMS/RCS, files, clipboard and media";
    mainProgram = "slonnect";
    platforms = lib.platforms.linux;
  };
}
