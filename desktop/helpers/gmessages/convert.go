package main

import (
	"strings"

	"go.mau.fi/mautrix-gmessages/pkg/libgm/gmproto"
)

// Status of a message, collapsed from Google's 40-odd values.
func statusOf(s gmproto.MessageStatusType) (status string, outgoing bool) {
	name := s.String()
	switch {
	case s == gmproto.MessageStatusType_MESSAGE_DELETED || s == gmproto.MessageStatusType_OUTGOING_DELETED:
		return "deleted", strings.HasPrefix(name, "OUTGOING")
	case strings.HasPrefix(name, "INCOMING"):
		return "received", false
	case s == gmproto.MessageStatusType_OUTGOING_COMPLETE:
		return "sent", true
	case s == gmproto.MessageStatusType_OUTGOING_DELIVERED:
		return "delivered", true
	case s == gmproto.MessageStatusType_OUTGOING_DISPLAYED:
		return "read", true
	case strings.HasPrefix(name, "OUTGOING_FAILED") || s == gmproto.MessageStatusType_OUTGOING_CANCELED ||
		s == gmproto.MessageStatusType_OUTGOING_RESTRICTED:
		return "failed", true
	case strings.HasPrefix(name, "OUTGOING"):
		return "pending", true
	}
	return "received", false
}

func participantOf(p *gmproto.Participant) Participant {
	name := p.GetFullName()
	if name == "" {
		name = p.GetFirstName()
	}
	number := p.GetID().GetNumber()
	if number == "" {
		number = p.GetFormattedNumber()
	}
	return Participant{
		ID:     p.GetID().GetParticipantID(),
		Number: number,
		Name:   name,
		Color:  p.GetAvatarHexColor(),
		IsMe:   p.GetIsMe(),
	}
}

func conversationOf(c *gmproto.Conversation) Conversation {
	out := Conversation{
		ID:       c.GetConversationID(),
		Name:     c.GetName(),
		Group:    c.GetIsGroupChat(),
		RCS:      c.GetType() == gmproto.ConversationType_RCS,
		Unread:   c.GetUnread(),
		ReadOnly: c.GetReadOnly(),
		Pinned:   c.GetPinned(),
		Date:     c.GetLastMessageTimestamp() / 1000,
		Snippet:  c.GetLatestMessage().GetDisplayContent(),
		FromMe:   c.GetLatestMessage().GetFromMe() != 0,
	}
	if !out.FromMe {
		out.SnippetFrom = c.GetLatestMessage().GetDisplayName()
	}
	out.Participants = []Participant{}
	for _, p := range c.GetParticipants() {
		if p.GetIsMe() || !p.GetIsVisible() && len(c.GetParticipants()) > 2 {
			continue
		}
		out.Participants = append(out.Participants, participantOf(p))
	}
	return out
}

// mediaRef is what we need to download one attachment later.
type mediaRef struct {
	key, thumbKey      []byte
	thumbMediaID, mime string
	name               string
}

// messageOf converts one message. `who` resolves participant IDs to people;
// `media` records how to fetch each attachment.
func messageOf(m *gmproto.Message, who func(participantID string) (Participant, bool), media func(id string, ref mediaRef)) Message {
	status, outgoing := statusOf(m.GetMessageStatus().GetStatus())
	out := Message{
		ID:             m.GetMessageID(),
		TmpID:          m.GetTmpID(),
		ConversationID: m.GetConversationID(),
		Date:           m.GetTimestamp() / 1000,
		Outgoing:       outgoing,
		Status:         status,
		StatusCode:     int32(m.GetMessageStatus().GetStatus()),
		Error:          m.GetMessageStatus().GetErrMsg(),
		ReplyTo:        m.GetReplyMessage().GetMessageID(),
		RCS:            m.GetType() == 4,
	}
	if out.Error == "" && status == "failed" {
		out.Error = m.GetMessageStatus().GetStatusText()
	}
	if !outgoing {
		if p, ok := who(m.GetParticipantID()); ok {
			out.Sender = &p
		} else if sp := m.GetSenderParticipant(); sp != nil {
			p := participantOf(sp)
			out.Sender = &p
		}
	}

	var text []string
	if s := m.GetSubject(); s != "" {
		text = append(text, s)
	}
	for _, info := range m.GetMessageInfo() {
		if c := info.GetMessageContent(); c != nil && c.GetContent() != "" {
			text = append(text, c.GetContent())
		}
		if mc := info.GetMediaContent(); mc != nil && mc.GetMediaID() != "" {
			out.Attachments = append(out.Attachments, Attachment{
				ID:       mc.GetMediaID(),
				Mime:     mc.GetMimeType(),
				Name:     mc.GetMediaName(),
				Size:     mc.GetSize(),
				Width:    mc.GetDimensions().GetWidth(),
				Height:   mc.GetDimensions().GetHeight(),
				HasThumb: mc.GetThumbnailMediaID() != "",
			})
			if media != nil {
				media(mc.GetMediaID(), mediaRef{
					key:          mc.GetDecryptionKey(),
					thumbMediaID: mc.GetThumbnailMediaID(),
					thumbKey:     mc.GetThumbnailDecryptionKey(),
					mime:         mc.GetMimeType(),
					name:         mc.GetMediaName(),
				})
			}
		}
		if loc := info.GetLocationContent(); loc != nil {
			text = append(text, locationText(loc))
		}
	}
	out.Text = strings.Join(text, "\n")

	// Reactions: one entry per emoji with who reacted.
	for _, r := range m.GetReactions() {
		emoji := r.GetData().GetUnicode()
		if emoji == "" {
			continue
		}
		mine := false
		for _, id := range r.GetParticipantIDs() {
			if p, ok := who(id); ok && p.IsMe {
				mine = true
			}
		}
		out.Reactions = append(out.Reactions, Reaction{Emoji: emoji, Count: len(r.GetParticipantIDs()), Mine: mine})
	}
	return out
}

func locationText(loc *gmproto.LocationContent) string {
	if t := loc.GetMessageContent().GetContent(); t != "" {
		return "📍 " + t
	}
	return "📍 Location"
}
