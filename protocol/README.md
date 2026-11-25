# Slonnect protocol extensions

Slonnect speaks the normal KDE Connect protocol (identity, pairing, TLS, all the stock
`kdeconnect.*` packets) and adds a few packet types under `kdeconnect.slonnect.*`. Stock KDE
Connect peers ignore them, because a plugin is only loaded when the other side lists its packet
types in its identity.

Who implements what:

| Side    | Code | Role |
|---------|------|------|
| Daemon  | `nix/kdeconnect-slonnect-plugin.patch` → `plugins/slonnect` in kdeconnect-kde | A pipe. Declares the packet types below, forwards received ones to D-Bus and sends what Slonnect hands it. No protocol logic. |
| Desktop | `desktop/src/slonnectlink.*` | Runs action graphs, including nodes that execute on the phone. Publishes its action list to the phone. |
| Mobile  | `mobile/src/main/java/dev/slonnect/mobile/` (`SlonnectPlugin`) | Executes phone nodes, publishes its node catalogue, shows and triggers the desktop's actions. |

All packets are ordinary KDE Connect packets: `{"id": <ms>, "type": "...", "body": {...}}`.
Unknown body fields must be ignored, so either side can add fields without a version bump.

## Handshake: `kdeconnect.slonnect.hello`

Sent by either side when the plugin comes up and whenever its catalogue changes.

```json
{
  "version": 1,
  "role": "desktop" | "mobile",
  "app": "slonnect 0.2.0",
  "features": ["actions", "nodes", "rcs-bridge"],
  "nodes": [ NodeDescriptor, ... ],   // what this side can execute for the other one
  "replyRequested": true
}
```

If `replyRequested` is true, the receiver answers with its own hello (`replyRequested: false`).
The desktop sends `replyRequested: true` when Slonnect starts or the phone becomes reachable, since
the daemon drops packets that arrive while Slonnect isn't running.

### NodeDescriptor

A node one side runs on behalf of the other. The desktop turns every mobile descriptor into a node
in the action editor (category *On the phone*) and keeps the last catalogue on disk, so graphs still
open while the phone is away.

```json
{
  "id": "bluetooth",                    // unique per side; the desktop registers it as "mobile.<id>"
  "title": "Bluetooth device",
  "category": "Phone",
  "icon": "bluetooth",                  // Material Symbols name
  "description": "Connect or disconnect a paired Bluetooth device on the phone.",
  "inputs":  [ PinDescriptor, ... ],
  "outputs": [ PinDescriptor, ... ],
  "flows":   ["out"],                   // outgoing flow pins; ["yes","no"] for a branch
  "available": true,                    // false: needs setup on the phone (see "unavailableReason")
  "unavailableReason": "Start Shizuku to let Slonnect connect Bluetooth devices"
}
```

`PinDescriptor`: `{"id", "label", "kind": "text"|"number"|"bool"|"choice", "default", "options": [..], "placeholder"}`
(the same pin kinds the desktop editor uses).

## Running a node: `kdeconnect.slonnect.node.run` / `.node.result`

```json
// desktop → mobile
{ "requestId": "r-42", "node": "bluetooth", "inputs": { "device": "Pixel Buds", "mode": "connect" }, "timeoutMs": 20000 }

// mobile → desktop
{ "requestId": "r-42", "ok": true, "flows": ["out"], "outputs": { "connected": true }, "log": ["Connected via Shizuku"] }
{ "requestId": "r-42", "ok": false, "error": "Pixel Buds isn't paired with this phone" }
```

The executor answers exactly once per `requestId`. The caller times out after `timeoutMs`
(default 30 s) and treats a missing reply as a failure. Several runs can be in flight at once;
nodes on both devices run concurrently when the graph forks with *Run in parallel*.

## Actions on the phone

```json
// desktop → mobile: the actions the phone may trigger (sent after hello and on every edit)
kdeconnect.slonnect.actions        { "actions": [ { "id": "swap-buds", "name": "Swap earbuds", "icon": "headphones",
                                                    "description": "...", "color": "#7f67be" } ] }
// mobile → desktop
kdeconnect.slonnect.actions.request { }
kdeconnect.slonnect.action.run      { "id": "swap-buds", "runId": "m-7" }
// desktop → mobile, at least once with a final state
kdeconnect.slonnect.action.status   { "runId": "m-7", "id": "swap-buds", "state": "running"|"done"|"failed",
                                      "error": "...", "step": "Bluetooth device" }
```

When Slonnect isn't running, the daemon plugin starts `slonnect --run-action <id> --device <device>`
for `action.run`, the same headless path the Run-command integration uses.

## RCS bridge

RCS doesn't add packet types. The phone's *Messages bridge* captures RCS (and other)
conversations that never reach Android's SMS provider and merges them into the regular SMS plugin
packets (`kdeconnect.sms.messages`), so kdeconnectd and every SMS client see them as ordinary
threads. Extra fields that stock clients ignore:

| Field (per message) | Meaning |
|---|---|
| `transport` | `"sms"`, `"mms"` or `"rcs"` |
| `sender`    | Display name of the sender in a group conversation, when there's no address |
| `bridge`    | `true` when the message came through the bridge instead of the SMS provider |

Bridged messages use thread IDs at or above `2^40` when the conversation has no telephony
thread, so they never collide with real ones. Sending (`kdeconnect.sms.request`) to such a thread,
or to any address while the bridge is set to *Always*, goes through the messaging app, which picks
RCS or SMS itself. See `mobile/SLONNECT.md` for how the bridge captures and sends messages and
where it falls short.
