package main

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/big"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/rs/zerolog"
	"go.mau.fi/util/exhttp"

	"go.mau.fi/mautrix-gmessages/pkg/libgm"
	"go.mau.fi/mautrix-gmessages/pkg/libgm/events"
	"go.mau.fi/mautrix-gmessages/pkg/libgm/gmproto"
)

// Provider is what the D-Bus layer drives: the real Google Messages session, or
// the synthetic one behind --demo.
type Provider interface {
	Start()
	Close()
	Status() Status
	StartLogin(cookies map[string]string)
	CancelLogin()
	Logout(ctx context.Context) error
	TakeOver(ctx context.Context) error
	SetAutoReactivate(on bool)
	ListConversations(ctx context.Context, count int) ([]Conversation, error)
	FetchMessages(ctx context.Context, conversationID string, count int, cursor string) (Page, error)
	Send(req SendRequest) (string, error)
	React(ctx context.Context, messageID, emoji string, add bool) error
	MarkRead(ctx context.Context, conversationID, messageID string) error
	SetTyping(ctx context.Context, conversationID string) error
	Delete(ctx context.Context, messageID string) error
	FetchMedia(ctx context.Context, mediaID string, thumbnail bool) (string, error)
	Avatar(ctx context.Context, participantID string) (string, error)
	ListContacts(ctx context.Context) ([]Contact, error)
}

var errNotConnected = errors.New("not signed in to Google Messages")

type Session struct {
	log   zerolog.Logger
	store *Store
	sink  Sink
	http  exhttp.ClientSettings

	mu             sync.Mutex
	client         *libgm.Client
	status         Status
	pairCancel     context.CancelFunc
	convs          map[string]*gmproto.Conversation
	people         map[string]Participant // participant ID -> person (incl. us)
	media          map[string]mediaRef    // media ID -> how to fetch it
	sims           []*gmproto.SIMCard
	autoReactivate bool
	reactivating   bool
	stop           chan struct{}
	saveLoopOnce   sync.Once
}

func NewSession(log zerolog.Logger, store *Store, sink Sink) *Session {
	return &Session{
		log:    log,
		store:  store,
		sink:   sink,
		http:   exhttp.SensibleClientSettings,
		status: Status{State: StateLoggedOut},
		convs:  map[string]*gmproto.Conversation{},
		people: map[string]Participant{},
		media:  map[string]mediaRef{},
		stop:   make(chan struct{}),
	}
}

// --- lifecycle -----------------------------------------------------------------

func (s *Session) Start() {
	data, err := s.store.Load()
	if err != nil {
		s.log.Warn().Err(err).Msg("Couldn't read the saved session")
	}
	if len(data) == 0 {
		s.setState(StateLoggedOut, "")
		return
	}
	ad := &libgm.AuthData{}
	if err := json.Unmarshal(data, ad); err != nil {
		s.log.Error().Err(err).Msg("Saved session is unreadable; sign in again")
		s.setState(StateLoggedOut, "The saved session was unreadable. Sign in again.")
		return
	}
	s.mu.Lock()
	s.client = s.newClient(ad)
	s.status.Account = ad.Mobile.GetSourceID()
	s.status.Secrets = s.store.Backend
	s.mu.Unlock()
	go s.connect()
	s.startSaveLoop()
}

func (s *Session) Close() {
	close(s.stop)
	s.mu.Lock()
	cli := s.client
	s.mu.Unlock()
	if cli != nil {
		s.save()
		cli.Disconnect()
	}
}

func (s *Session) newClient(ad *libgm.AuthData) *libgm.Client {
	cli := libgm.NewClient(ad, nil, s.log.With().Str("component", "libgm").Logger(), s.http)
	// Too short and the phone's battery suffers (the bridge moved to 20 minutes for this).
	cli.SetPingInterval(20 * time.Minute)
	cli.SetEventHandler(s.handleEvent)
	return cli
}

func (s *Session) connect() {
	s.mu.Lock()
	cli := s.client
	s.mu.Unlock()
	if cli == nil {
		return
	}
	s.setState(StateConnecting, "")
	if err := cli.Connect(context.Background()); err != nil {
		s.log.Err(err).Msg("Connecting failed")
		if isCredentialError(err) {
			s.setState(StateNeedsRelogin, "Google rejected the saved sign-in. Sign in again.")
		} else {
			s.setState(StateConnecting, "Can't reach Google Messages: "+err.Error())
			time.AfterFunc(time.Minute, s.connect)
		}
	}
}

func isCredentialError(err error) bool {
	return errors.Is(err, events.ErrInvalidCredentials) || errors.Is(err, events.ErrRequestedEntityNotFound) || errors.Is(err, libgm.ErrNoCookies)
}

func (s *Session) save() {
	s.mu.Lock()
	cli := s.client
	s.mu.Unlock()
	if cli == nil || cli.AuthData == nil {
		return
	}
	data, err := json.Marshal(cli.AuthData)
	if err != nil {
		s.log.Err(err).Msg("Couldn't serialise the session")
		return
	}
	if err := s.store.Save(data); err != nil {
		s.log.Err(err).Msg("Couldn't save the session")
		return
	}
	s.mu.Lock()
	changed := s.status.Secrets != s.store.Backend
	s.status.Secrets = s.store.Backend
	st := s.status
	s.mu.Unlock()
	if changed {
		s.sink.StateChanged(st)
	}
}

func (s *Session) startSaveLoop() { s.saveLoopOnce.Do(func() { go s.saveLoop() }) }

// Google refreshes cookies and tokens as we go; keep the stored copy current.
func (s *Session) saveLoop() {
	t := time.NewTicker(15 * time.Minute)
	defer t.Stop()
	for {
		select {
		case <-s.stop:
			return
		case <-t.C:
			s.save()
		}
	}
}

func (s *Session) setState(state, errText string) {
	s.mu.Lock()
	if s.status.State == state && s.status.Error == errText {
		s.mu.Unlock()
		return
	}
	s.status.State = state
	s.status.Error = errText
	st := s.status
	s.mu.Unlock()
	s.log.Info().Str("state", state).Msg("State changed")
	s.sink.StateChanged(st)
}

func (s *Session) Status() Status {
	s.mu.Lock()
	defer s.mu.Unlock()
	st := s.status
	st.SIMs = nil
	for _, sim := range s.sims {
		st.SIMs = append(st.SIMs, SIM{
			ID:      sim.GetSIMParticipant().GetID(),
			Number:  sim.GetSIMData().GetFormattedPhoneNumber(),
			Carrier: sim.GetSIMData().GetCarrierName(),
			Color:   sim.GetSIMData().GetColorHex(),
		})
	}
	return st
}

func (s *Session) cli() (*libgm.Client, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.client == nil || s.status.State == StatePairing {
		return nil, errNotConnected
	}
	return s.client, nil
}

// --- login -----------------------------------------------------------------------

func (s *Session) StartLogin(cookies map[string]string) {
	s.CancelLogin()
	ctx, cancel := context.WithCancel(context.Background())
	s.mu.Lock()
	s.pairCancel = cancel
	existing := s.client
	relogin := s.status.State == StateNeedsRelogin
	s.mu.Unlock()
	s.setState(StatePairing, "")

	go func() {
		defer cancel()
		// Same account, expired cookies: the existing pairing may take the new cookies
		// without a new emoji round trip, which keeps conversation IDs stable.
		if relogin && existing != nil && s.relogin(ctx, existing, cookies) {
			return
		}
		s.pair(ctx, cookies)
	}()
}

func (s *Session) relogin(ctx context.Context, cli *libgm.Client, cookies map[string]string) bool {
	oldAccount := cli.AuthData.Mobile.GetSourceID()
	cli.AuthData.SetCookies(cookies)
	if err := cli.FetchConfig(ctx); err != nil {
		s.log.Debug().Err(err).Msg("Existing pairing didn't take the new cookies")
		return false
	}
	if cli.Config.GetDeviceInfo().GetEmail() != oldAccount {
		s.log.Warn().Msg("New cookies are for a different account; pairing afresh")
		return false
	}
	s.mu.Lock()
	s.pairCancel = nil
	s.mu.Unlock()
	s.save()
	s.sink.PairingFinished(true, "")
	go s.connect()
	return true
}

func (s *Session) pair(ctx context.Context, cookies map[string]string) {
	fail := func(msg string, err error) {
		s.log.Warn().Err(err).Msg("Pairing failed")
		s.mu.Lock()
		s.pairCancel = nil
		hadClient := s.client != nil && s.client.IsLoggedIn()
		s.mu.Unlock()
		if hadClient {
			s.setState(StateNeedsRelogin, msg)
		} else {
			s.setState(StateLoggedOut, msg)
		}
		s.sink.PairingFinished(false, msg)
	}

	ad := libgm.NewAuthData()
	ad.Cookies = cookies
	cli := libgm.NewClient(ad, nil, s.log.With().Str("component", "libgm").Str("phase", "pairing").Logger(), s.http)
	cli.SetEventHandler(func(evt any) { s.log.Debug().Type("event", evt).Msg("Event while pairing") })
	if err := cli.FetchConfig(ctx); err != nil {
		fail("Google didn't accept these cookies. Sign in again in a private window and copy them fresh.", err)
		return
	}
	emoji, sess, err := cli.StartGaiaPairing(ctx, ctx)
	if err != nil {
		cli.Disconnect()
		fail(pairingError(err), err)
		return
	}
	s.sink.PairingEmoji(emoji, libgm.GetEmojiSVG(emoji))

	if _, err := cli.FinishGaiaPairing(ctx, sess); err != nil {
		cli.Disconnect()
		fail(pairingError(err), err)
		return
	}
	cli.Disconnect()

	s.mu.Lock()
	if s.client != nil {
		s.client.Disconnect()
	}
	s.client = s.newClient(cli.AuthData)
	s.pairCancel = nil
	s.status.Account = cli.AuthData.Mobile.GetSourceID()
	s.convs = map[string]*gmproto.Conversation{}
	s.mu.Unlock()
	s.save()
	s.sink.PairingFinished(true, "")
	go s.connect()
	s.startSaveLoop()
}

func pairingError(err error) string {
	switch {
	case errors.Is(err, libgm.ErrNoDevicesFound):
		return "No phone with Google Messages is signed in to this Google account."
	case errors.Is(err, libgm.ErrPhoneNotFound), errors.Is(err, libgm.ErrPhoneInactive):
		return "Google Messages on the phone isn't reachable. Open it on the phone and try again."
	case errors.Is(err, libgm.ErrPairingInitTimeout):
		return "The phone didn't answer. Open Google Messages on the phone and try again."
	case errors.Is(err, libgm.ErrIncorrectEmoji):
		return "The wrong emoji was tapped on the phone. Try again."
	case errors.Is(err, libgm.ErrPairingCancelled):
		return "Pairing was cancelled on the phone."
	case errors.Is(err, libgm.ErrPairingTimeout):
		return "Nobody tapped the emoji in time. Try again."
	case errors.Is(err, context.Canceled):
		return "Cancelled."
	}
	return "Pairing failed: " + err.Error()
}

func (s *Session) CancelLogin() {
	s.mu.Lock()
	cancel := s.pairCancel
	s.pairCancel = nil
	s.mu.Unlock()
	if cancel != nil {
		cancel()
	}
}

func (s *Session) Logout(ctx context.Context) error {
	s.CancelLogin()
	s.mu.Lock()
	cli := s.client
	s.client = nil
	s.convs = map[string]*gmproto.Conversation{}
	s.people = map[string]Participant{}
	s.media = map[string]mediaRef{}
	s.sims = nil
	s.status = Status{State: StateLoggedOut}
	st := s.status
	s.mu.Unlock()
	var err error
	if cli != nil {
		err = cli.Unpair(ctx)
		cli.Disconnect()
	}
	s.store.Clear()
	s.sink.StateChanged(st)
	return err
}

func (s *Session) TakeOver(ctx context.Context) error {
	cli, err := s.cli()
	if err != nil {
		return err
	}
	return cli.SetActiveSession(ctx)
}

func (s *Session) SetAutoReactivate(on bool) {
	s.mu.Lock()
	s.autoReactivate = on
	s.mu.Unlock()
}

// reactivate takes the active session back after another Messages for Web tab
// grabbed it, backing off so we don't fight forever.
func (s *Session) reactivate() {
	s.mu.Lock()
	if s.reactivating {
		s.mu.Unlock()
		return
	}
	s.reactivating = true
	s.mu.Unlock()
	defer func() { s.mu.Lock(); s.reactivating = false; s.mu.Unlock() }()
	for _, wait := range []time.Duration{5 * time.Second, 10 * time.Second, 30 * time.Second} {
		time.Sleep(wait)
		if s.Status().State != StateInactive {
			return
		}
		if err := s.TakeOver(context.Background()); err == nil {
			return
		}
	}
}

// --- events ------------------------------------------------------------------------

func (s *Session) handleEvent(rawEvt any) {
	switch evt := rawEvt.(type) {
	case *events.ClientReady:
		for _, c := range evt.Conversations {
			s.rememberConversation(c)
			s.sink.ConversationChanged(conversationOf(c))
		}
		s.setState(StateReady, "")
	case *events.AuthTokenRefreshed:
		go s.save()
	case *gmproto.Conversation:
		s.rememberConversation(evt)
		if evt.GetStatus() == gmproto.ConversationStatus_DELETED {
			s.sink.ConversationRemoved(evt.GetConversationID())
		} else {
			s.sink.ConversationChanged(conversationOf(evt))
		}
	case *libgm.WrappedMessage:
		s.sink.MessageChanged(s.convertMessage(evt.Message), !evt.IsOld)
	case *gmproto.TypingData:
		s.sink.Typing(evt.GetConversationID(), evt.GetUser().GetNumber(), evt.GetType() == gmproto.TypingTypes_STARTED_TYPING)
	case *gmproto.UserAlertEvent:
		switch evt.GetAlertType() {
		case gmproto.AlertType_BROWSER_ACTIVE:
			s.setState(StateReady, "")
		case gmproto.AlertType_BROWSER_INACTIVE, gmproto.AlertType_BROWSER_INACTIVE_FROM_TIMEOUT, gmproto.AlertType_BROWSER_INACTIVE_FROM_INACTIVITY:
			s.setState(StateInactive, "Google Messages is open in another browser.")
			s.mu.Lock()
			auto := s.autoReactivate
			s.mu.Unlock()
			if auto {
				go s.reactivate()
			}
		}
	case *gmproto.Settings:
		s.mu.Lock()
		s.sims = evt.GetSIMCards()
		s.mu.Unlock()
		s.sink.StateChanged(s.Status())
	case *events.PhoneNotResponding:
		s.setState(StatePhoneOffline, "The phone isn't responding. Check that it's online.")
	case *events.PhoneRespondingAgain, *events.ListenRecovered:
		s.setState(StateReady, "")
	case *events.ListenTemporaryError:
		s.setState(StateConnecting, "Connection problem: "+evt.Error.Error())
	case *events.ListenFatalError:
		if isCredentialError(evt.Error) {
			s.setState(StateNeedsRelogin, "Google signed this session out. Sign in again.")
		} else {
			s.setState(StateConnecting, "Disconnected: "+evt.Error.Error())
			time.AfterFunc(time.Minute, s.connect)
		}
	case *events.GaiaLoggedOut:
		s.setState(StateNeedsRelogin, "Google signed this session out. Sign in again.")
	case *gmproto.RevokePairData:
		s.log.Info().Msg("Unpaired from the phone")
		_ = s.Logout(context.Background())
	case *events.PingFailed:
		if errors.Is(evt.Error, events.ErrRequestedEntityNotFound) {
			s.setState(StateNeedsRelogin, "The phone no longer knows this computer. Sign in again.")
		}
	case *events.HackySetActiveMayFail:
		go func() {
			time.Sleep(7 * time.Second)
			if s.Status().State != StateReady {
				_ = s.TakeOver(context.Background())
			}
		}()
	}
}

func (s *Session) rememberConversation(c *gmproto.Conversation) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.convs[c.GetConversationID()] = c
	for _, p := range c.GetParticipants() {
		if id := p.GetID().GetParticipantID(); id != "" {
			s.people[id] = participantOf(p)
		}
	}
}

func (s *Session) convertMessage(m *gmproto.Message) Message {
	return messageOf(m,
		func(id string) (Participant, bool) {
			s.mu.Lock()
			defer s.mu.Unlock()
			p, ok := s.people[id]
			return p, ok
		},
		func(id string, ref mediaRef) {
			s.mu.Lock()
			s.media[id] = ref
			s.mu.Unlock()
		})
}

func (s *Session) conversation(ctx context.Context, id string) (*gmproto.Conversation, error) {
	s.mu.Lock()
	c := s.convs[id]
	s.mu.Unlock()
	if c != nil {
		return c, nil
	}
	cli, err := s.cli()
	if err != nil {
		return nil, err
	}
	c, err = cli.GetConversation(ctx, id)
	if err != nil {
		return nil, err
	}
	s.rememberConversation(c)
	return c, nil
}

// --- requests ------------------------------------------------------------------------

func (s *Session) ListConversations(ctx context.Context, count int) ([]Conversation, error) {
	cli, err := s.cli()
	if err != nil {
		return nil, err
	}
	resp, err := cli.ListConversations(ctx, &gmproto.ListConversationsRequest{Count: int64(count), Folder: gmproto.ListConversationsRequest_INBOX})
	if err != nil {
		return nil, err
	}
	out := make([]Conversation, 0, len(resp.GetConversations()))
	for _, c := range resp.GetConversations() {
		s.rememberConversation(c)
		out = append(out, conversationOf(c))
	}
	return out, nil
}

func (s *Session) FetchMessages(ctx context.Context, conversationID string, count int, cursor string) (Page, error) {
	cli, err := s.cli()
	if err != nil {
		return Page{}, err
	}
	// Know the participants so senders resolve.
	if _, err := s.conversation(ctx, conversationID); err != nil {
		s.log.Debug().Err(err).Msg("Couldn't load conversation before fetching messages")
	}
	var cur *gmproto.Cursor
	if id, ts, ok := strings.Cut(cursor, "|"); ok {
		t, _ := strconv.ParseInt(ts, 10, 64)
		cur = &gmproto.Cursor{LastItemID: id, LastItemTimestamp: t}
	}
	resp, err := cli.FetchMessages(ctx, conversationID, int64(count), cur)
	if err != nil {
		return Page{}, err
	}
	page := Page{Messages: []Message{}}
	for _, m := range resp.GetMessages() { // newest first
		// Going backwards, the phone repeats the cursor message and may include newer ones.
		if cur != nil && (m.GetMessageID() == cur.LastItemID || m.GetTimestamp()/1000 >= cur.LastItemTimestamp) {
			continue
		}
		page.Messages = append(page.Messages, s.convertMessage(m))
	}
	if c := resp.GetCursor(); c != nil && c.GetLastItemID() != "" && len(resp.GetMessages()) > 0 {
		page.Cursor = c.GetLastItemID() + "|" + strconv.FormatInt(c.GetLastItemTimestamp(), 10)
		page.More = true
	}
	return page, nil
}

// simFor picks the SIM to send from: the requested one, else the conversation's own.
func (s *Session) simFor(c *gmproto.Conversation, requested string) (*gmproto.SIMPayload, string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	want := requested
	if want == "" {
		want = c.GetDefaultOutgoingID()
	}
	for _, sim := range s.sims {
		if sim.GetSIMParticipant().GetID() == want {
			return sim.GetSIMData().GetSIMPayload(), want
		}
	}
	return nil, c.GetDefaultOutgoingID()
}

func newTmpID() string {
	n, _ := rand.Int(rand.Reader, big.NewInt(1_000_000_000_000))
	return fmt.Sprintf("tmp_%012d", n)
}

func (s *Session) Send(req SendRequest) (string, error) {
	cli, err := s.cli()
	if err != nil {
		return "", err
	}
	if strings.TrimSpace(req.Text) == "" && len(req.Files) == 0 {
		return "", errors.New("nothing to send")
	}
	if req.ConversationID == "" && len(req.Numbers) == 0 {
		return "", errors.New("no conversation or recipients")
	}
	for _, f := range req.Files {
		if st, err := os.Stat(f); err != nil || st.IsDir() {
			return "", fmt.Errorf("can't read %s", filepath.Base(f))
		}
	}
	if req.TmpID == "" {
		req.TmpID = newTmpID()
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
		defer cancel()
		if err := s.send(ctx, cli, req); err != nil {
			s.log.Warn().Err(err).Str("tmp_id", req.TmpID).Msg("Send failed")
			s.sink.SendFailed(req.TmpID, req.ConversationID, err.Error())
		}
	}()
	return req.TmpID, nil
}

func (s *Session) send(ctx context.Context, cli *libgm.Client, req SendRequest) error {
	if req.ConversationID == "" {
		c, err := s.createConversation(ctx, cli, req.Numbers)
		if err != nil {
			return err
		}
		req.ConversationID = c.GetConversationID()
	}
	conv, err := s.conversation(ctx, req.ConversationID)
	if err != nil {
		return err
	}
	simPayload, outgoingID := s.simFor(conv, req.SIM)

	// One message per part: the text, then each file. RCS rejects captions on media.
	var parts []*gmproto.MessageInfo
	if t := strings.TrimSpace(req.Text); t != "" {
		parts = append(parts, &gmproto.MessageInfo{Data: &gmproto.MessageInfo_MessageContent{MessageContent: &gmproto.MessageContent{Content: t}}})
	}
	for _, f := range req.Files {
		data, err := os.ReadFile(f)
		if err != nil {
			return err
		}
		mt := mime.TypeByExtension(strings.ToLower(filepath.Ext(f)))
		if mt == "" {
			mt = http.DetectContentType(data)
		}
		if i := strings.IndexByte(mt, ';'); i > 0 {
			mt = mt[:i]
		}
		media, err := cli.UploadMedia(ctx, data, filepath.Base(f), mt)
		if err != nil {
			return fmt.Errorf("uploading %s: %w", filepath.Base(f), err)
		}
		parts = append(parts, &gmproto.MessageInfo{Data: &gmproto.MessageInfo_MediaContent{MediaContent: media}})
	}
	for i, part := range parts {
		tmpID := req.TmpID
		if i > 0 {
			tmpID = fmt.Sprintf("%s_%d", req.TmpID, i)
		}
		msg := &gmproto.SendMessageRequest{
			ConversationID: req.ConversationID,
			MessagePayload: &gmproto.MessagePayload{
				TmpID:          tmpID,
				ConversationID: req.ConversationID,
				ParticipantID:  outgoingID,
				TmpID2:         tmpID,
				MessageInfo:    []*gmproto.MessageInfo{part},
			},
			SIMPayload: simPayload,
			TmpID:      tmpID,
		}
		if req.ReplyTo != "" && i == 0 && conv.GetType() == gmproto.ConversationType_RCS {
			msg.Reply = &gmproto.ReplyPayload{MessageID: req.ReplyTo}
		}
		resp, err := cli.SendMessage(ctx, msg)
		if err != nil {
			return err
		}
		if resp.GetStatus() != gmproto.SendMessageResponse_SUCCESS {
			return fmt.Errorf("the phone refused the message (%s)", resp.GetStatus())
		}
	}
	return nil
}

func (s *Session) createConversation(ctx context.Context, cli *libgm.Client, numbers []string) (*gmproto.Conversation, error) {
	req := &gmproto.GetOrCreateConversationRequest{}
	for _, n := range numbers {
		req.Numbers = append(req.Numbers, &gmproto.ContactNumber{MysteriousInt: 2, Number: n, Number2: n})
	}
	resp, err := cli.GetOrCreateConversation(ctx, req)
	if err == nil && resp.GetStatus() == gmproto.GetOrCreateConversationResponse_CREATE_RCS {
		empty, yes := "", true
		req.RCSGroupName = &empty
		req.CreateRCSGroup = &yes
		resp, err = cli.GetOrCreateConversation(ctx, req)
	}
	if err != nil {
		return nil, err
	}
	c := resp.GetConversation()
	if c.GetConversationID() == "" {
		return nil, fmt.Errorf("the phone didn't create the conversation (%s)", resp.GetStatus())
	}
	s.rememberConversation(c)
	s.sink.ConversationChanged(conversationOf(c))
	return c, nil
}

func (s *Session) React(ctx context.Context, messageID, emoji string, add bool) error {
	cli, err := s.cli()
	if err != nil {
		return err
	}
	action := gmproto.SendReactionRequest_ADD
	if !add {
		action = gmproto.SendReactionRequest_REMOVE
	}
	resp, err := cli.SendReaction(ctx, &gmproto.SendReactionRequest{MessageID: messageID, ReactionData: gmproto.MakeReactionData(emoji), Action: action})
	if err != nil {
		return err
	}
	if !resp.GetSuccess() {
		return errors.New("the phone rejected the reaction")
	}
	return nil
}

func (s *Session) MarkRead(ctx context.Context, conversationID, messageID string) error {
	cli, err := s.cli()
	if err != nil {
		return err
	}
	return cli.MarkRead(ctx, conversationID, messageID)
}

func (s *Session) SetTyping(ctx context.Context, conversationID string) error {
	cli, err := s.cli()
	if err != nil {
		return err
	}
	conv, err := s.conversation(ctx, conversationID)
	if err != nil {
		return err
	}
	sim, _ := s.simFor(conv, "")
	return cli.SetTyping(ctx, conversationID, sim)
}

func (s *Session) Delete(ctx context.Context, messageID string) error {
	cli, err := s.cli()
	if err != nil {
		return err
	}
	resp, err := cli.DeleteMessage(ctx, messageID)
	if err != nil {
		return err
	}
	if !resp.GetSuccess() {
		return errors.New("the phone didn't delete the message")
	}
	return nil
}

func extensionFor(ref mediaRef) string {
	if ext := filepath.Ext(ref.name); ext != "" && len(ext) <= 6 {
		return ext
	}
	if exts, _ := mime.ExtensionsByType(ref.mime); len(exts) > 0 {
		return exts[len(exts)-1]
	}
	return ".bin"
}

// FetchMedia downloads and decrypts one attachment (or its thumbnail) into the
// cache and returns the file's path.
func (s *Session) FetchMedia(ctx context.Context, mediaID string, thumbnail bool) (string, error) {
	s.mu.Lock()
	ref, ok := s.media[mediaID]
	s.mu.Unlock()
	if !ok {
		return "", errors.New("unknown attachment; reload the conversation")
	}
	id, key, ext := mediaID, ref.key, extensionFor(ref)
	sub := "media"
	if thumbnail {
		if ref.thumbMediaID == "" {
			return "", errors.New("no thumbnail")
		}
		id, key, ext, sub = ref.thumbMediaID, ref.thumbKey, ".jpg", "thumbnails"
	}
	path := cachePath(sub, id, ext)
	if st, err := os.Stat(path); err == nil && st.Size() > 0 {
		return path, nil
	}
	cli, err := s.cli()
	if err != nil {
		return "", err
	}
	body, err := cli.DownloadMedia(ctx, id, key)
	if err != nil {
		return "", err
	}
	defer body.Close()
	return path, writeAtomically(path, body)
}

func writeAtomically(path string, r io.Reader) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), ".part-*")
	if err != nil {
		return err
	}
	if _, err := io.Copy(tmp, r); err != nil {
		tmp.Close()
		os.Remove(tmp.Name())
		return err
	}
	if err := tmp.Close(); err != nil {
		os.Remove(tmp.Name())
		return err
	}
	return os.Rename(tmp.Name(), path)
}

// Avatar returns a cached photo for a participant, or "" when they have none.
func (s *Session) Avatar(ctx context.Context, participantID string) (string, error) {
	path := cachePath("avatars", participantID, ".jpg")
	if st, err := os.Stat(path); err == nil && time.Since(st.ModTime()) < 24*time.Hour {
		if st.Size() == 0 {
			return "", nil // known to have no photo
		}
		return path, nil
	}
	cli, err := s.cli()
	if err != nil {
		return "", err
	}
	resp, err := cli.GetParticipantThumbnail(ctx, participantID)
	if err != nil {
		return "", err
	}
	var data []byte
	for _, t := range resp.GetThumbnail() {
		if t.GetIdentifier() == participantID {
			data = t.GetData().GetImageBuffer()
		}
	}
	if err := os.WriteFile(path, data, 0o600); err != nil {
		return "", err
	}
	if len(data) == 0 {
		return "", nil
	}
	return path, nil
}

func (s *Session) ListContacts(ctx context.Context) ([]Contact, error) {
	cli, err := s.cli()
	if err != nil {
		return nil, err
	}
	resp, err := cli.ListContacts(ctx)
	if err != nil {
		return nil, err
	}
	out := make([]Contact, 0, len(resp.GetContacts()))
	for _, c := range resp.GetContacts() {
		number := c.GetNumber().GetNumber()
		if number == "" {
			number = c.GetNumber().GetFormattedNumber()
		}
		out = append(out, Contact{ID: c.GetContactID(), ParticipantID: c.GetParticipantID(), Name: c.GetName(), Number: number, Color: c.GetAvatarHexColor()})
	}
	return out, nil
}
