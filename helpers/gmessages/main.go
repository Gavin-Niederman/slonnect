// slonnect-gm: Google Messages for Web, on the session bus, for Slonnect.
//
// It pairs with the Google Messages app on the phone (Google account cookies plus
// an emoji confirmation) using libgm from mautrix-gmessages, keeps the session
// alive, and serves conversations as dev.slonnect.GMessages1. See README.md.
package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"github.com/godbus/dbus/v5"
	"github.com/godbus/dbus/v5/introspect"
	"github.com/godbus/dbus/v5/prop"
	"github.com/rs/zerolog"

	"go.mau.fi/mautrix-gmessages/pkg/libgm/gmproto"
	"go.mau.fi/mautrix-gmessages/pkg/libgm/util"
)

func main() {
	demo := flag.Bool("demo", false, "serve synthetic conversations instead of a real Google Messages session")
	fileSecrets := flag.Bool("file-secrets", false, "keep the session in a 0600 file instead of the Secret Service")
	verbose := flag.Bool("v", false, "debug logging (IDs and states only; never message content)")
	flag.Parse()

	level := zerolog.InfoLevel
	if *verbose {
		level = zerolog.DebugLevel
	}
	log := zerolog.New(zerolog.ConsoleWriter{Out: os.Stderr, NoColor: true, PartsExclude: []string{zerolog.TimestampFieldName}}).
		Level(level).With().Timestamp().Logger()

	// How this computer shows up in the phone's list of paired devices. A tablet slot
	// leaves the browser slot free for messages.google.com.
	util.BrowserDetailsMessage.OS = "Slonnect"
	util.BrowserDetailsMessage.BrowserType = gmproto.BrowserType_OTHER
	util.BrowserDetailsMessage.DeviceType = gmproto.DeviceType_TABLET

	conn, err := dbus.ConnectSessionBus()
	if err != nil {
		fmt.Fprintln(os.Stderr, "slonnect-gm: no session bus:", err)
		os.Exit(1)
	}
	defer conn.Close()

	signals := &Signals{conn: conn, log: log}
	var provider Provider
	if *demo {
		provider = NewDemo(signals)
	} else {
		provider = NewSession(log, &Store{ForceFile: *fileSecrets}, signals)
	}
	methods := &Methods{p: provider, log: log}

	if err := conn.Export(methods, ObjectPath, Interface); err != nil {
		fmt.Fprintln(os.Stderr, "slonnect-gm: export:", err)
		os.Exit(1)
	}
	props, err := prop.Export(conn, ObjectPath, prop.Map{
		Interface: {
			"State":           {Value: StateLoggedOut, Emit: prop.EmitTrue},
			"Account":         {Value: "", Emit: prop.EmitTrue},
			"ProtocolVersion": {Value: ProtocolVersion, Emit: prop.EmitConst},
		},
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, "slonnect-gm: properties:", err)
		os.Exit(1)
	}
	signals.props = props
	if err := conn.Export(introspect.NewIntrospectable(introspection(methods, props)), ObjectPath, "org.freedesktop.DBus.Introspectable"); err != nil {
		fmt.Fprintln(os.Stderr, "slonnect-gm: introspection:", err)
		os.Exit(1)
	}

	reply, err := conn.RequestName(BusName, dbus.NameFlagDoNotQueue)
	if err != nil || reply != dbus.RequestNameReplyPrimaryOwner {
		fmt.Fprintln(os.Stderr, "slonnect-gm: another instance owns", BusName)
		os.Exit(1)
	}

	provider.Start()
	signals.StateChanged(provider.Status())
	log.Info().Bool("demo", *demo).Msg("Serving " + BusName)

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	<-stop
	log.Info().Msg("Shutting down")
	provider.Close()
}
