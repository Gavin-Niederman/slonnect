// Wire types: what crosses D-Bus to Slonnect, as JSON. Deliberately smaller and
// flatter than libgm's protos, and stable across libgm updates.
package main

// State values, also the D-Bus State property.
const (
	StateLoggedOut    = "logged_out"
	StatePairing      = "pairing"
	StateConnecting   = "connecting"
	StateReady        = "ready"
	StatePhoneOffline = "phone_offline"
	StateInactive     = "inactive" // another Messages for Web session is active
	StateNeedsRelogin = "needs_relogin"
)

type Status struct {
	State   string `json:"state"`
	Account string `json:"account,omitempty"`
	Phone   string `json:"phone,omitempty"`
	Error   string `json:"error,omitempty"`
	Secrets string `json:"secrets,omitempty"` // "keyring" | "file"
	SIMs    []SIM  `json:"sims,omitempty"`
}

type SIM struct {
	ID      string `json:"id"` // the SIM's own participant ID
	Number  string `json:"number,omitempty"`
	Carrier string `json:"carrier,omitempty"`
	Color   string `json:"color,omitempty"`
}

type Participant struct {
	ID     string `json:"id"`
	Number string `json:"number,omitempty"`
	Name   string `json:"name,omitempty"`
	Color  string `json:"color,omitempty"`
	IsMe   bool   `json:"isMe,omitempty"`
}

type Conversation struct {
	ID           string        `json:"id"`
	Name         string        `json:"name,omitempty"`
	Participants []Participant `json:"participants"` // everyone but us
	Group        bool          `json:"group,omitempty"`
	RCS          bool          `json:"rcs,omitempty"`
	Unread       bool          `json:"unread,omitempty"`
	ReadOnly     bool          `json:"readOnly,omitempty"`
	Pinned       bool          `json:"pinned,omitempty"`
	Date         int64         `json:"date"` // ms since epoch
	Snippet      string        `json:"snippet,omitempty"`
	SnippetFrom  string        `json:"snippetFrom,omitempty"`
	FromMe       bool          `json:"fromMe,omitempty"`
}

type Attachment struct {
	ID       string `json:"id"` // media ID
	Mime     string `json:"mime,omitempty"`
	Name     string `json:"name,omitempty"`
	Size     int64  `json:"size,omitempty"`
	Width    int64  `json:"width,omitempty"`
	Height   int64  `json:"height,omitempty"`
	HasThumb bool   `json:"hasThumb,omitempty"`
}

type Reaction struct {
	Emoji string `json:"emoji"`
	Count int    `json:"count"`
	Mine  bool   `json:"mine,omitempty"`
}

type Message struct {
	ID             string `json:"id"`
	TmpID          string `json:"tmpId,omitempty"`
	ConversationID string `json:"conversationId"`
	Date           int64  `json:"date"` // ms since epoch
	Outgoing       bool   `json:"outgoing,omitempty"`
	// pending | sent | delivered | read | failed | received | deleted
	Status      string       `json:"status"`
	StatusCode  int32        `json:"statusCode,omitempty"`
	Error       string       `json:"error,omitempty"`
	Sender      *Participant `json:"sender,omitempty"`
	Text        string       `json:"text,omitempty"`
	Attachments []Attachment `json:"attachments,omitempty"`
	ReplyTo     string       `json:"replyTo,omitempty"`
	Reactions   []Reaction   `json:"reactions,omitempty"`
	RCS         bool         `json:"rcs,omitempty"`
}

type Page struct {
	Messages []Message `json:"messages"` // newest first
	Cursor   string    `json:"cursor,omitempty"`
	More     bool      `json:"more"`
}

type Contact struct {
	ID            string `json:"id"`
	ParticipantID string `json:"participantId,omitempty"`
	Name          string `json:"name"`
	Number        string `json:"number"`
	Color         string `json:"color,omitempty"`
}

type SendRequest struct {
	ConversationID string   `json:"conversationId,omitempty"`
	Numbers        []string `json:"numbers,omitempty"` // for a new conversation
	Text           string   `json:"text,omitempty"`
	Files          []string `json:"files,omitempty"`
	ReplyTo        string   `json:"replyTo,omitempty"`
	SIM            string   `json:"sim,omitempty"`
	TmpID          string   `json:"tmpId,omitempty"`
}

// Callbacks into the D-Bus layer; implemented by Service.
type Sink interface {
	StateChanged(Status)
	PairingEmoji(emoji, svg string)
	PairingFinished(ok bool, err string)
	ConversationChanged(Conversation)
	ConversationRemoved(id string)
	MessageChanged(m Message, live bool)
	Typing(conversationID, number string, typing bool)
	SendFailed(tmpID, conversationID, err string)
}
