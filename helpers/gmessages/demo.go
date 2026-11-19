package main

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"os"
	"sort"
	"strings"
	"sync"
	"time"
)

// Demo serves synthetic conversations over the real D-Bus interface, so Slonnect's
// Google Messages backend can be exercised without a Google account (--demo).
type Demo struct {
	sink Sink

	mu       sync.Mutex
	state    string
	convs    map[string]*Conversation
	messages map[string][]Message // newest first
	pairing  context.CancelFunc
	seq      int
}

func NewDemo(sink Sink) *Demo {
	return &Demo{sink: sink, convs: map[string]*Conversation{}, messages: map[string][]Message{}}
}

var (
	demoMe    = Participant{ID: "me", Number: "+15550100000", Name: "You", IsMe: true}
	demoSam   = Participant{ID: "p1", Number: "+15550100001", Name: "Sam Okafor", Color: "#7B5CC4"}
	demoRobin = Participant{ID: "p2", Number: "+15550100002", Name: "Robin Vale", Color: "#2E7D6B"}
	demoAlex  = Participant{ID: "p3", Number: "+15550100003", Name: "Alex Kim", Color: "#B4552A"}
	demoBank  = Participant{ID: "p4", Number: "72400", Name: "72400"}
)

func (d *Demo) Start() {
	now := time.Now().UnixMilli()
	min := int64(60_000)
	d.mu.Lock()
	d.state = StateReady
	add := func(c Conversation, msgs ...Message) {
		for i := range msgs {
			msgs[i].ConversationID = c.ID
			msgs[i].RCS = c.RCS
			if msgs[i].Status == "" {
				msgs[i].Status = "received"
				if msgs[i].Outgoing {
					msgs[i].Status = "read"
				}
			}
		}
		sort.Slice(msgs, func(a, b int) bool { return msgs[a].Date > msgs[b].Date })
		last := msgs[0]
		c.Date, c.Snippet, c.FromMe = last.Date, last.Text, last.Outgoing
		if c.Snippet == "" && len(last.Attachments) > 0 {
			c.Snippet = "Photo"
		}
		d.convs[c.ID] = &c
		d.messages[c.ID] = msgs
	}
	add(Conversation{ID: "c1", Name: demoSam.Name, Participants: []Participant{demoSam}, RCS: true},
		Message{ID: "m1", Date: now - 26*60*min, Sender: &demoSam, Text: "Are we still on for the climb on Saturday?"},
		Message{ID: "m2", Date: now - 26*60*min + min, Outgoing: true, Text: "Yes! 9am at the north trailhead",
			Reactions: []Reaction{{Emoji: "👍", Count: 1}}},
		Message{ID: "m3", Date: now - 40*min, Sender: &demoSam, Attachments: []Attachment{{ID: "img1", Mime: "image/png", Name: "route.png", Width: 320, Height: 200, HasThumb: true}}},
		Message{ID: "m4", Date: now - 39*min, Sender: &demoSam, Text: "This is the route I was thinking of", ReplyTo: "m2"},
		Message{ID: "m5", Date: now - 3*min, Outgoing: true, Text: "Looks great. I'll bring the rope 🧗", Status: "delivered"},
	)
	add(Conversation{ID: "c2", Name: "Climbing crew", Participants: []Participant{demoSam, demoRobin, demoAlex}, Group: true, RCS: true, Unread: true, SnippetFrom: "Robin"},
		Message{ID: "g1", Date: now - 5*60*min, Sender: &demoAlex, Text: "Who has the spare harness?"},
		Message{ID: "g2", Date: now - 5*60*min + 2*min, Outgoing: true, Text: "I do, I'll bring it"},
		Message{ID: "g3", Date: now - 12*min, Sender: &demoRobin, Text: "Carpool leaves at 8 from my place",
			Reactions: []Reaction{{Emoji: "❤️", Count: 2, Mine: true}}},
	)
	add(Conversation{ID: "c3", Name: demoBank.Name, Participants: []Participant{demoBank}},
		Message{ID: "s1", Date: now - 3*24*60*min, Sender: &demoBank, Text: "Your verification code is 482913. It expires in 10 minutes."},
	)
	d.mu.Unlock()
}

func (d *Demo) Close() {}

func (d *Demo) Status() Status {
	d.mu.Lock()
	defer d.mu.Unlock()
	st := Status{State: d.state, Secrets: "demo"}
	if d.state != StateLoggedOut && d.state != StatePairing {
		st.Account = "demo@example.com"
		st.SIMs = []SIM{{ID: "sim1", Number: "+1 555-010-0000", Carrier: "Demo Mobile"}, {ID: "sim2", Number: "+1 555-010-0099", Carrier: "Travel eSIM"}}
	}
	return st
}

func (d *Demo) setState(state string) {
	d.mu.Lock()
	d.state = state
	d.mu.Unlock()
	d.sink.StateChanged(d.Status())
}

func (d *Demo) StartLogin(map[string]string) {
	ctx, cancel := context.WithCancel(context.Background())
	d.mu.Lock()
	d.pairing = cancel
	d.mu.Unlock()
	d.setState(StatePairing)
	go func() {
		select {
		case <-ctx.Done():
			d.setState(StateLoggedOut)
			d.sink.PairingFinished(false, "Cancelled.")
			return
		case <-time.After(time.Second):
		}
		d.sink.PairingEmoji("🦊", "")
		select {
		case <-ctx.Done():
			d.setState(StateLoggedOut)
			d.sink.PairingFinished(false, "Cancelled.")
		case <-time.After(4 * time.Second):
			d.sink.PairingFinished(true, "")
			d.Start()
			d.setState(StateReady)
			for _, c := range d.sortedConversations() {
				d.sink.ConversationChanged(c)
			}
		}
	}()
}

func (d *Demo) CancelLogin() {
	d.mu.Lock()
	cancel := d.pairing
	d.mu.Unlock()
	if cancel != nil {
		cancel()
	}
}

func (d *Demo) Logout(context.Context) error {
	d.mu.Lock()
	d.convs = map[string]*Conversation{}
	d.messages = map[string][]Message{}
	d.mu.Unlock()
	d.setState(StateLoggedOut)
	return nil
}

func (d *Demo) TakeOver(context.Context) error { d.setState(StateReady); return nil }
func (d *Demo) SetAutoReactivate(bool)         {}

func (d *Demo) ready() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.state != StateReady {
		return errNotConnected
	}
	return nil
}

func (d *Demo) sortedConversations() []Conversation {
	d.mu.Lock()
	defer d.mu.Unlock()
	out := make([]Conversation, 0, len(d.convs))
	for _, c := range d.convs {
		out = append(out, *c)
	}
	sort.Slice(out, func(a, b int) bool { return out[a].Date > out[b].Date })
	return out
}

func (d *Demo) ListConversations(context.Context, int) ([]Conversation, error) {
	if err := d.ready(); err != nil {
		return nil, err
	}
	return d.sortedConversations(), nil
}

func (d *Demo) FetchMessages(_ context.Context, id string, count int, cursor string) (Page, error) {
	if err := d.ready(); err != nil {
		return Page{}, err
	}
	time.Sleep(300 * time.Millisecond) // the phone takes a moment
	d.mu.Lock()
	defer d.mu.Unlock()
	all := d.messages[id]
	start := 0
	if cursor != "" {
		for i, m := range all {
			if m.ID == cursor {
				start = i + 1
			}
		}
	}
	end := min(start+count, len(all))
	page := Page{Messages: append([]Message{}, all[start:end]...)}
	if end < len(all) {
		page.Cursor, page.More = all[end-1].ID, true
	}
	return page, nil
}

func (d *Demo) Send(req SendRequest) (string, error) {
	if err := d.ready(); err != nil {
		return "", err
	}
	if strings.TrimSpace(req.Text) == "" && len(req.Files) == 0 {
		return "", errors.New("nothing to send")
	}
	d.mu.Lock()
	if req.ConversationID == "" {
		d.seq++
		id := fmt.Sprintf("new%d", d.seq)
		var people []Participant
		for i, n := range req.Numbers {
			people = append(people, Participant{ID: fmt.Sprintf("%s-%d", id, i), Number: n})
		}
		d.convs[id] = &Conversation{ID: id, Participants: people, Group: len(people) > 1, RCS: true}
		req.ConversationID = id
	}
	c, ok := d.convs[req.ConversationID]
	if !ok {
		d.mu.Unlock()
		return "", errors.New("no such conversation")
	}
	conv := *c
	d.seq++
	tmpID := req.TmpID
	if tmpID == "" {
		tmpID = fmt.Sprintf("tmp_demo%d", d.seq)
	}
	msg := Message{ID: fmt.Sprintf("out%d", d.seq), TmpID: tmpID, ConversationID: req.ConversationID, Date: time.Now().UnixMilli(),
		Outgoing: true, Status: "pending", Text: strings.TrimSpace(req.Text), ReplyTo: req.ReplyTo, RCS: conv.RCS}
	for i, f := range req.Files {
		msg.Attachments = append(msg.Attachments, Attachment{ID: fmt.Sprintf("%s-f%d", msg.ID, i), Name: f, Mime: "application/octet-stream"})
	}
	d.mu.Unlock()

	go func() {
		steps := []struct {
			after  time.Duration
			status string
		}{{0, "pending"}, {700 * time.Millisecond, "sent"}, {1500 * time.Millisecond, "delivered"}, {3 * time.Second, "read"}}
		for _, st := range steps {
			time.Sleep(st.after)
			msg.Status = st.status
			d.store(msg)
			d.sink.MessageChanged(msg, true)
		}
		// Somebody answers 1:1 chats.
		if conv.Group || len(conv.Participants) == 0 {
			return
		}
		who := conv.Participants[0]
		d.sink.Typing(conv.ID, who.Number, true)
		time.Sleep(2 * time.Second)
		d.sink.Typing(conv.ID, who.Number, false)
		d.mu.Lock()
		d.seq++
		reply := Message{ID: fmt.Sprintf("in%d", d.seq), ConversationID: conv.ID, Date: time.Now().UnixMilli(), Status: "received",
			Sender: &who, Text: "Sounds good! 👍", RCS: conv.RCS}
		d.mu.Unlock()
		d.store(reply)
		d.sink.MessageChanged(reply, true)
	}()
	return tmpID, nil
}

// store records a message and bumps its conversation.
func (d *Demo) store(m Message) {
	d.mu.Lock()
	list := d.messages[m.ConversationID]
	replaced := false
	for i := range list {
		if list[i].ID == m.ID {
			list[i], replaced = m, true
		}
	}
	if !replaced {
		list = append([]Message{m}, list...)
	}
	d.messages[m.ConversationID] = list
	c := d.convs[m.ConversationID]
	c.Date, c.Snippet, c.FromMe = m.Date, m.Text, m.Outgoing
	c.Unread = !m.Outgoing
	conv := *c
	d.mu.Unlock()
	d.sink.ConversationChanged(conv)
}

func (d *Demo) findMessage(id string) (Message, bool) {
	d.mu.Lock()
	defer d.mu.Unlock()
	for _, list := range d.messages {
		for _, m := range list {
			if m.ID == id {
				return m, true
			}
		}
	}
	return Message{}, false
}

func (d *Demo) React(_ context.Context, id, emoji string, add bool) error {
	m, ok := d.findMessage(id)
	if !ok {
		return errors.New("no such message")
	}
	var out []Reaction
	found := false
	for _, r := range m.Reactions {
		if r.Emoji == emoji {
			found = true
			if add && !r.Mine {
				r.Count, r.Mine = r.Count+1, true
			} else if !add && r.Mine {
				r.Count, r.Mine = r.Count-1, false
			}
		}
		if r.Count > 0 {
			out = append(out, r)
		}
	}
	if !found && add {
		out = append(out, Reaction{Emoji: emoji, Count: 1, Mine: true})
	}
	m.Reactions = out
	d.mu.Lock()
	for i, x := range d.messages[m.ConversationID] {
		if x.ID == id {
			d.messages[m.ConversationID][i] = m
		}
	}
	d.mu.Unlock()
	d.sink.MessageChanged(m, true)
	return nil
}

func (d *Demo) MarkRead(_ context.Context, conv, _ string) error {
	d.mu.Lock()
	c := d.convs[conv]
	if c == nil || !c.Unread {
		d.mu.Unlock()
		return nil
	}
	c.Unread = false
	snapshot := *c
	d.mu.Unlock()
	d.sink.ConversationChanged(snapshot)
	return nil
}

func (d *Demo) SetTyping(context.Context, string) error { return nil }

func (d *Demo) Delete(_ context.Context, id string) error {
	m, ok := d.findMessage(id)
	if !ok {
		return errors.New("no such message")
	}
	m.Status = "deleted"
	d.mu.Lock()
	list := d.messages[m.ConversationID]
	for i, x := range list {
		if x.ID == id {
			d.messages[m.ConversationID] = append(list[:i:i], list[i+1:]...)
			break
		}
	}
	d.mu.Unlock()
	d.sink.MessageChanged(m, true)
	return nil
}

func (d *Demo) FetchMedia(_ context.Context, id string, thumbnail bool) (string, error) {
	w, h := 320, 200
	if thumbnail {
		w, h = 160, 100
	}
	path := cachePath("demo", fmt.Sprintf("%s-%v", id, thumbnail), ".png")
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			// A sunset over a ridge line.
			ridge := h*2/3 + int(float64(h)/8*float64((x*7)%w)/float64(w)) - h/16
			if y > ridge {
				img.Set(x, y, color.RGBA{50, 60, 90, 255})
			} else {
				img.Set(x, y, color.RGBA{uint8(250 - y/2), uint8(150 + y/3), uint8(90 + y/2), 255})
			}
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		return "", err
	}
	return path, writeAtomically(path, &buf)
}

func (d *Demo) Avatar(context.Context, string) (string, error) { return "", nil }

func (d *Demo) ListContacts(context.Context) ([]Contact, error) {
	var out []Contact
	for _, p := range []Participant{demoSam, demoRobin, demoAlex} {
		out = append(out, Contact{ID: p.ID, ParticipantID: p.ID, Name: p.Name, Number: p.Number, Color: p.Color})
	}
	return out, nil
}
