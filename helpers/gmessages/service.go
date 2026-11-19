package main

import (
	"context"
	"encoding/json"
	"time"

	"github.com/godbus/dbus/v5"
	"github.com/godbus/dbus/v5/introspect"
	"github.com/godbus/dbus/v5/prop"
	"github.com/rs/zerolog"
)

const (
	BusName         = "dev.slonnect.GMessages"
	ObjectPath      = dbus.ObjectPath("/dev/slonnect/GMessages")
	Interface       = "dev.slonnect.GMessages1"
	ProtocolVersion = uint32(1)

	errFailed     = Interface + ".Error.Failed"
	errBadRequest = Interface + ".Error.BadRequest"
)

// Methods exported on Interface. Objects travel as JSON strings (see types.go).
type Methods struct {
	p   Provider
	log zerolog.Logger
}

func (m *Methods) ctx() (context.Context, context.CancelFunc) {
	return context.WithTimeout(context.Background(), 2*time.Minute)
}

func fail(err error) *dbus.Error {
	if err == nil {
		return nil
	}
	return dbus.NewError(errFailed, []any{err.Error()})
}

func encode(v any) (string, *dbus.Error) {
	b, err := json.Marshal(v)
	if err != nil {
		return "", fail(err)
	}
	return string(b), nil
}

func (m *Methods) Status() (string, *dbus.Error) { return encode(m.p.Status()) }

// StartLogin takes pasted cookies (JSON, a cURL command or a Cookie header).
// Progress arrives as PairingEmoji and PairingFinished.
func (m *Methods) StartLogin(cookies string) *dbus.Error {
	parsed, err := parseCookies(cookies)
	if err != nil {
		return dbus.NewError(errBadRequest, []any{err.Error()})
	}
	m.p.StartLogin(parsed)
	return nil
}

func (m *Methods) CancelLogin() *dbus.Error { m.p.CancelLogin(); return nil }

func (m *Methods) Logout() *dbus.Error {
	ctx, cancel := m.ctx()
	defer cancel()
	return fail(m.p.Logout(ctx))
}

func (m *Methods) TakeOver() *dbus.Error {
	ctx, cancel := m.ctx()
	defer cancel()
	return fail(m.p.TakeOver(ctx))
}

func (m *Methods) SetAutoReactivate(on bool) *dbus.Error { m.p.SetAutoReactivate(on); return nil }

func (m *Methods) ListConversations(count uint32) (string, *dbus.Error) {
	ctx, cancel := m.ctx()
	defer cancel()
	if count == 0 || count > 500 {
		count = 100
	}
	list, err := m.p.ListConversations(ctx, int(count))
	if err != nil {
		return "", fail(err)
	}
	return encode(list)
}

func (m *Methods) FetchMessages(conversationID string, count uint32, cursor string) (string, *dbus.Error) {
	ctx, cancel := m.ctx()
	defer cancel()
	if count == 0 || count > 200 {
		count = 40
	}
	page, err := m.p.FetchMessages(ctx, conversationID, int(count), cursor)
	if err != nil {
		return "", fail(err)
	}
	return encode(page)
}

// Send takes a JSON SendRequest and returns the temporary ID that the message's
// MessageChanged updates (or SendFailed) will carry.
func (m *Methods) Send(request string) (string, *dbus.Error) {
	var req SendRequest
	if err := json.Unmarshal([]byte(request), &req); err != nil {
		return "", dbus.NewError(errBadRequest, []any{err.Error()})
	}
	tmpID, err := m.p.Send(req)
	if err != nil {
		return "", fail(err)
	}
	return tmpID, nil
}

func (m *Methods) React(messageID, emoji string, add bool) *dbus.Error {
	ctx, cancel := m.ctx()
	defer cancel()
	return fail(m.p.React(ctx, messageID, emoji, add))
}

func (m *Methods) MarkRead(conversationID, messageID string) *dbus.Error {
	ctx, cancel := m.ctx()
	defer cancel()
	return fail(m.p.MarkRead(ctx, conversationID, messageID))
}

func (m *Methods) SetTyping(conversationID string) *dbus.Error {
	ctx, cancel := m.ctx()
	defer cancel()
	return fail(m.p.SetTyping(ctx, conversationID))
}

func (m *Methods) Delete(messageID string) *dbus.Error {
	ctx, cancel := m.ctx()
	defer cancel()
	return fail(m.p.Delete(ctx, messageID))
}

func (m *Methods) FetchMedia(mediaID string, thumbnail bool) (string, *dbus.Error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancel()
	path, err := m.p.FetchMedia(ctx, mediaID, thumbnail)
	return path, fail(err)
}

func (m *Methods) Avatar(participantID string) (string, *dbus.Error) {
	ctx, cancel := m.ctx()
	defer cancel()
	path, err := m.p.Avatar(ctx, participantID)
	return path, fail(err)
}

func (m *Methods) ListContacts() (string, *dbus.Error) {
	ctx, cancel := m.ctx()
	defer cancel()
	list, err := m.p.ListContacts(ctx)
	if err != nil {
		return "", fail(err)
	}
	return encode(list)
}

// Signals implements Sink by emitting D-Bus signals.
type Signals struct {
	conn  *dbus.Conn
	props *prop.Properties
	log   zerolog.Logger
}

func (s *Signals) emit(name string, args ...any) {
	if err := s.conn.Emit(ObjectPath, Interface+"."+name, args...); err != nil {
		s.log.Warn().Err(err).Str("signal", name).Msg("Couldn't emit signal")
	}
}

func (s *Signals) json(v any) string {
	b, _ := json.Marshal(v)
	return string(b)
}

func (s *Signals) StateChanged(st Status) {
	if s.props != nil {
		s.props.SetMust(Interface, "State", st.State)
		s.props.SetMust(Interface, "Account", st.Account)
	}
	s.emit("StatusChanged", s.json(st))
}
func (s *Signals) PairingEmoji(emoji, svg string)          { s.emit("PairingEmoji", emoji, svg) }
func (s *Signals) PairingFinished(ok bool, err string)     { s.emit("PairingFinished", ok, err) }
func (s *Signals) ConversationChanged(c Conversation)      { s.emit("ConversationChanged", s.json(c)) }
func (s *Signals) ConversationRemoved(id string)           { s.emit("ConversationRemoved", id) }
func (s *Signals) MessageChanged(m Message, live bool)     { s.emit("MessageChanged", s.json(m), live) }
func (s *Signals) Typing(conv, number string, typing bool) { s.emit("Typing", conv, number, typing) }
func (s *Signals) SendFailed(tmpID, conv, err string)      { s.emit("SendFailed", tmpID, conv, err) }

func introspection(methods *Methods, props *prop.Properties) *introspect.Node {
	arg := func(name, sig string) introspect.Arg { return introspect.Arg{Name: name, Type: sig} }
	return &introspect.Node{
		Name: string(ObjectPath),
		Interfaces: []introspect.Interface{
			introspect.IntrospectData,
			prop.IntrospectData,
			{
				Name:       Interface,
				Methods:    introspect.Methods(methods),
				Properties: props.Introspection(Interface),
				Signals: []introspect.Signal{
					{Name: "StatusChanged", Args: []introspect.Arg{arg("status", "s")}},
					{Name: "PairingEmoji", Args: []introspect.Arg{arg("emoji", "s"), arg("svg", "s")}},
					{Name: "PairingFinished", Args: []introspect.Arg{arg("ok", "b"), arg("error", "s")}},
					{Name: "ConversationChanged", Args: []introspect.Arg{arg("conversation", "s")}},
					{Name: "ConversationRemoved", Args: []introspect.Arg{arg("id", "s")}},
					{Name: "MessageChanged", Args: []introspect.Arg{arg("message", "s"), arg("live", "b")}},
					{Name: "Typing", Args: []introspect.Arg{arg("conversationId", "s"), arg("number", "s"), arg("typing", "b")}},
					{Name: "SendFailed", Args: []introspect.Arg{arg("tmpId", "s"), arg("conversationId", "s"), arg("error", "s")}},
				},
			},
		},
	}
}
