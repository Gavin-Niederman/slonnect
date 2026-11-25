# slonnect-gm

Google Messages for Web on the session bus, so Slonnect can use it instead of KDE
Connect for SMS, MMS and RCS. It pairs with the Google Messages app on the phone the
same way messages.google.com does, using [libgm](https://github.com/mautrix/gmessages/tree/main/pkg/libgm)
from mautrix-gmessages, which reverse-engineers that protocol.

AGPL-3.0 (see `LICENSE`), because libgm is. It's a separate program on purpose: Slonnect
only talks to it over D-Bus.

## What to expect

- **The phone does the work.** Every message goes through Google Messages on the phone,
  relayed by Google, so the phone must be online. It doesn't have to be on your network.
- **Sign-in needs Google session cookies.** Google no longer offers QR pairing to
  third-party clients. Sign in at
  `https://accounts.google.com/AccountChooser?continue=https://messages.google.com/web/config`
  in a private Firefox window, copy the `/web/config` request as cURL from devtools, and
  paste it into Slonnect (Settings → Messages). Then tap the emoji Slonnect shows on
  the phone. Chrome's device-bound session cookies usually can't be copied.
- **It can break.** Google changes the protocol now and then; libgm is pinned in `go.mod`
  and updated with mautrix-gmessages releases. KDE Connect stays one switch away.
- It pairs as a *tablet*, so messages.google.com in a browser keeps working. If a browser
  makes itself the active session, this one goes `inactive` until you press *Use here*
  (or turn on automatic take-back in Slonnect).
- Google's terms don't mention unofficial clients either way. We know of no account
  actions over it, but there's no permission either.

## Where things live

- Session (cookies and pairing keys): the Secret Service, item `slonnect-gmessages`; or
  `$XDG_STATE_HOME/slonnect/gmessages/session.json` (0600) when there is none, or with
  `--file-secrets`.
- Downloaded media and avatars: `$XDG_CACHE_HOME/slonnect/gmessages/`.
- Logs (stderr / the journal): states and IDs only, never message text or numbers.

## D-Bus interface

`dev.slonnect.GMessages`, object `/dev/slonnect/GMessages`, interface
`dev.slonnect.GMessages1`. Objects are JSON strings; shapes are in `types.go`.

| Kind | Name | |
|---|---|---|
| property | `State` s, `Account` s, `ProtocolVersion` u | `logged_out`, `pairing`, `connecting`, `ready`, `phone_offline`, `inactive`, `needs_relogin` |
| method | `Status() → s` | state, account, error, SIMs, where secrets are kept |
| method | `StartLogin(s pasted)`, `CancelLogin()`, `Logout()`, `TakeOver()`, `SetAutoReactivate(b)` | |
| method | `ListConversations(u count) → s` | |
| method | `FetchMessages(s conversation, u count, s cursor) → s` | `{messages (newest first), cursor, more}` |
| method | `Send(s request) → s tmpId` | request: `{conversationId \| numbers, text, files, replyTo, sim, tmpId}` |
| method | `React(s message, s emoji, b add)`, `MarkRead(s conversation, s message)`, `SetTyping(s conversation)`, `Delete(s message)` | |
| method | `FetchMedia(s mediaId, b thumbnail) → s path`, `Avatar(s participant) → s path`, `ListContacts() → s` | |
| signal | `StatusChanged(s)`, `PairingEmoji(s emoji, s svg)`, `PairingFinished(b ok, s error)` | |
| signal | `ConversationChanged(s)`, `ConversationRemoved(s)`, `MessageChanged(s message, b live)`, `Typing(s conversation, s number, b)`, `SendFailed(s tmpId, s conversation, s error)` | |

## Running

```sh
nix build .#gmessages            # ./result/bin/slonnect-gm
slonnect-gm --demo               # synthetic conversations, no Google account
go test ./...                    # from desktop/helpers/gmessages
```

With Home Manager, `services.slonnect.googleMessages.enable = true;` runs it as a user
service. It's also D-Bus activatable, so Slonnect starts it on demand when it's installed.
