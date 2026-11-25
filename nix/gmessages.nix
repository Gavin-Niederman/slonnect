# slonnect-gm: Google Messages for Web for Slonnect (desktop/helpers/gmessages). AGPL-3.0,
# since it builds on libgm from mautrix-gmessages.
{ lib, buildGoModule }:

buildGoModule {
  pname = "slonnect-gm";
  version = "0.1.0";
  src = ../desktop/helpers/gmessages;
  vendorHash = "sha256-mvpjnKdTcpnyTR5XZtWzR+YNGsvdHFWcWdqU29GTtq4=";
  env.CGO_ENABLED = 0;

  postInstall = ''
    mv $out/bin/gmessages $out/bin/slonnect-gm
    # D-Bus activation, so Slonnect can start it on demand without the HM service.
    mkdir -p $out/share/dbus-1/services
    cat > $out/share/dbus-1/services/dev.slonnect.GMessages.service <<SERVICE
    [D-BUS Service]
    Name=dev.slonnect.GMessages
    Exec=$out/bin/slonnect-gm
    SERVICE
  '';

  meta = {
    description = "Google Messages for Web on the session bus, for Slonnect";
    license = lib.licenses.agpl3Plus;
    mainProgram = "slonnect-gm";
    platforms = lib.platforms.linux;
  };
}
