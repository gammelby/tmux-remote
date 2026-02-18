# tmux-remote Design

## 1. System Overview

tmux-remote provides secure remote terminal access to tmux sessions using Nabto Edge peer-to-peer connectivity. It replaces SSH with a zero-configuration model: no port forwarding, firewall rules or dynamic DNS. A lightweight agent runs on the host machine and relays PTY I/O over encrypted Nabto connections to authenticated clients.

```
 iOS App / Android App / CLI Client
       |
  [Nabto Edge P2P / DTLS]
       |
   tmux-remote Agent
       |
   tmux session <-> PTY
```

## 2. Components

| Component | Language | SDK | Purpose |
|-----------|----------|-----|---------|
| `agent/` | C | Nabto Embedded SDK | Serves terminal sessions, relays PTY data, detects patterns |
| `clients/cli/` | C | Nabto Client SDK | Command-line terminal client |
| `clients/ios/` | Swift | NabtoEdgeClientSwift | Mobile terminal with pattern overlay UI |
| `clients/android/` | Kotlin | Nabto Edge Client SDK for Android | Mobile terminal with pattern overlay UI |

The agent is tool-agnostic. It knows nothing about what runs inside the terminal. Pattern definitions are loaded from JSON configuration files and evaluated generically.

## 3. Communication Model

### Nabto Streams

| Port | Name | Transport | Payload | Purpose |
|------|------|-----------|---------|---------|
| 1 | Data stream | Nabto stream | Raw PTY bytes | Bidirectional PTY relay, identical to SSH |
| 2 | Control stream | Nabto stream (non-CoAP) | Length-prefixed CBOR | Session state, prompt lifecycle events, client resolve |

### CoAP Endpoints

All require IAM authorization. Payloads are CBOR over CoAP (content format 60).

CoAP and stream protocols are separate:
- CoAP is used for request/response endpoints (`/terminal/attach`, `/terminal/resize`, `/terminal/sessions`, ...).
- Port 2 control traffic is a stream protocol (framed CBOR), not CoAP.

| Method | Path | Purpose |
|--------|------|---------|
| POST | `/terminal/attach` | Set target tmux session for next stream open |
| POST | `/terminal/resize` | Set PTY size, send SIGWINCH |
| POST | `/terminal/create` | Create a new tmux session |
| GET | `/terminal/sessions` | List tmux sessions |
| GET | `/terminal/status` | Agent health and uptime |

## 4. Security Model

tmux-remote grants remote shell access. A compromise means an attacker can execute arbitrary commands as the user running the agent. This is equivalent to SSH access. The security posture must match or exceed SSH.

- **Single role: Owner.** Full access or no access. No "limited" roles. A multi-role model would be security theater for a remote shell.
- **Password Invite pairing only.** A one-time password is generated per invitation and closed after use. No other pairing mode is enabled.
- **Every endpoint checks IAM.** CoAP handlers and stream listeners call `tmuxremote_iam_check_access()`/`tmuxremote_iam_check_access_ref()` before processing. Unpaired connections can only access pairing endpoints.
- **DTLS with ECC.** All traffic is encrypted end-to-end by the Nabto platform. The basestation mediates connection setup but cannot decrypt traffic.
- **Device key at-rest storage is explicit.** The device private key is stored either in macOS Keychain or in a namespaced key file under `~/.tmux-remote/keys/`.

### Security Properties

- **End-to-end encryption**: DTLS with ECC key pairs. The Nabto basestation mediates connection setup (hole punching, relay fallback) but cannot decrypt traffic.
- **Mutual authentication**: After pairing, both client and device authenticate each other via public key fingerprints on every connection. No passwords are transmitted or stored after the initial pairing.
- **Zero exposed ports**: The device agent opens no listening TCP or UDP ports. All connectivity is outbound to the Nabto basestation. No attack surface for port scanners.
- **One-time pairing passwords**: Pairing passwords are single-use. After a client pairs, the password is invalidated on the device. A leaked pairing string that has already been used is worthless.
- **PTY isolation**: The PTY is spawned as the user running the agent. tmux session access is limited to that user's sessions. The agent does not run as root.

### Comparison with SSH

| Property | SSH | tmux-remote |
|----------|-----|------------|
| Key exchange | `ssh-keygen` + copy public key to server | Pairing string (one-time password for PAKE key exchange) |
| Authentication | Public key or password per connection | Public key (after initial pairing) |
| Encryption | AES/ChaCha20 over TCP | DTLS with ECC over UDP |
| Network requirements | Open port 22, firewall rules, possibly dynamic DNS | None. Outbound UDP only. |
| NAT traversal | Requires port forwarding or relay (ngrok, etc.) | Built-in P2P hole punching with relay fallback |

The trust model is identical: both rely on a one-time key exchange followed by public key authentication. tmux-remote's pairing is arguably more user-friendly than SSH's `ssh-copy-id` workflow, while providing the same cryptographic guarantees.

### Pairing Flow

The pairing flow is identical across all clients. The transport is the same (Nabto Client SDK); only the UI differs.

1. On `--init`, the agent pre-creates an initial owner invitation with a generated one-time password and SCT.
2. The invite pairing string is printed during `--init` and shown on startup while no client has paired yet.
3. The user copies it to their client (CLI command or mobile app paste).
4. The client parses the pairing string, generates a keypair if needed, connects and completes the PAKE-based key exchange. Public keys are exchanged and stored.
5. The invitation is consumed. Pairing is now closed. No further clients can pair.
6. To add another device, run `tmux-remote-agent --add-user <name>` at the server terminal.

### Agent CLI

#### `tmux-remote-agent --init`

Interactive first-time setup:

```
$ tmux-remote-agent --init
No configuration found. Creating initial setup.

Product ID: pr-xxxxxxxx
Device ID:  de-yyyyyyyy
Store device key in macOS Keychain? [Y/n] y

Generated device keypair.
Key storage: macOS Keychain
Fingerprint: 08c955a5f7505f16f03bc3e3e0db89ff56ce571e0dd6be153c5bae9174d62ac6

Register this fingerprint in the Nabto Cloud Console before starting.

Configuration written to ~/.tmux-remote/
```

#### Normal startup (no users paired yet)

```
$ tmux-remote-agent

######## tmux-remote ########
# Product ID:     pr-xxxxxxxx
# Device ID:      de-yyyyyyyy
# Fingerprint:    08c955a5...
# Version:        0.1.0
# Key storage:    macOS Keychain
#
#  No users paired yet. Pair your phone by copying
#  this string into the tmux-remote app:
#
#  p=pr-xxxxxxxx,d=de-yyyyyyyy,u=owner,pwd=CbAHaqpKKrhK,sct=TUfe3n3hhhM9
#
#  This is a one-time pairing password. After you pair,
#  it is invalidated and pairing is closed.
#
######## Waiting for pairing... ########
```

#### `tmux-remote-agent --add-user <name>`

Creates a one-time invitation for an additional client:

```
$ tmux-remote-agent --add-user tablet
Created invitation for user 'tablet'.

Copy this string into the tmux-remote app on the new device:

  p=pr-xxxxxxxx,d=de-yyyyyyyy,u=tablet,pwd=xK9mRtYwZp3n,sct=Hf7kL2nQwR4s

This invitation can only be used once. Pairing will close
again after this device pairs.
```

#### `tmux-remote-agent --remove-user <name>`

Revokes access for a paired user or cancels an unused invitation:

```
$ tmux-remote-agent --remove-user tablet
Removed user 'tablet'. Their public key has been deleted.
They will no longer be able to connect.
```

#### `tmux-remote-agent --demo-init`

Removed. The agent enforces invite-only pairing. Use `--init` for first setup and `--add-user` for additional one-time invitations.

### Agent Configuration Directory

```
~/.tmux-remote/
  config/device.json          # Product ID, Device ID, server settings, KeychainKey preference
  config/iam_config.json      # IAM policies, roles (static)
  state/iam_state.json        # Paired users and pending one-time invitations (mutable)
  keys/<productId>_<deviceId>.key  # Device private key (file-storage mode only)
  patterns/*.json             # Pattern definitions
```

### Device Key Storage (Agent)

- The agent device private key always remains local and is never sent to the basestation.
- Storage mode is selected during `--init` on macOS:
  - Interactive TTY: prompts `Store device key in macOS Keychain? [Y/n]`.
  - Non-interactive mode: defaults to file storage; set `TMUX_REMOTE_KEY_STORAGE=keychain` to opt into keychain storage.
- The selection is persisted as `KeychainKey` in `config/device.json`.
- Key naming is per device identity (`productId`, `deviceId`) to avoid collisions:
  - macOS Keychain account: `p=<productId>,d=<deviceId>` (service: `dk.ulrik.tmux-remote.devicekey`)
  - File path: `~/.tmux-remote/keys/<productId>_<deviceId>.key` (invalid filename characters are sanitized to `_`)
- Runtime fallback:
  - If keychain storage is requested but unavailable, the agent falls back to file storage and logs a warning.
  - The startup banner prints the actual storage used (`macOS Keychain` or concrete key file path).

## 5. CLI Client

A command-line client for connecting to a tmux-remote agent from another computer. Uses the Nabto Client SDK (binary release). The CLI client is the simplest client and serves as the primary driver for developing and testing the agent. It is a transparent byte pipe: connects, opens a Nabto stream and relays bytes between the stream and the local terminal's stdin/stdout.

### Usage

```
tmux-remote pair <pairing-string>                    # One-time pairing
tmux-remote attach <device-name> [session]            # Attach to existing tmux session
tmux-remote create <device-name> [session] [command]  # Create new session, optionally run command
tmux-remote sessions <device-name>                    # List tmux sessions
tmux-remote devices                                   # List saved devices
tmux-remote rename <device-name> <new-name>           # Rename a saved device
```

Aliases: `a` for `attach`, `c`/`n`/`new`/`new-session` for `create`.

### Attach/Create Flow

1. Look up device bookmark from `~/.tmux-remote-client/` by name.
2. Create a Nabto client connection with stored product ID, device ID, client private key and SCT.
3. `nabto_client_connection_connect()`.
4. Send `POST /terminal/attach` or `POST /terminal/create` over CoAP (CBOR payload) with session name and terminal dimensions from `ioctl(TIOCGWINSZ)`.
5. Open stream on port 1 via `nabto_client_stream_open()`.
6. Set local terminal to raw mode (`cfmakeraw`).
7. Enter relay loop: `select()` on stdin and the Nabto stream fd (or use the async API with futures).
8. Handle `SIGWINCH`: send `POST /terminal/resize` via CoAP (CBOR payload) when the local terminal is resized.
9. On stream close or EOF, restore terminal and exit.

### Client Configuration

```
~/.tmux-remote-client/
  client.key              # Client private key
  devices.json            # Saved device bookmarks
```

## 6. Mobile Apps (iOS and Android)

The iOS and Android apps are functionally identical. Both are pure display layers for the agent: they render terminal output, display pattern overlay prompts from the agent's control stream and send user actions back. Neither app performs pattern detection.

| Aspect | iOS | Android |
|--------|-----|---------|
| Language | Swift | Kotlin |
| UI framework | SwiftUI | Jetpack Compose |
| Terminal emulator | SwiftTerm | Termux terminal-emulator/terminal-view |
| Nabto SDK | NabtoEdgeClientSwift (CocoaPods) | Nabto Edge Client SDK (Maven Central) |
| CBOR library | SwiftCBOR | Jackson CBOR |
| Key storage | iOS Keychain | Android EncryptedSharedPreferences |
| Persistence | UserDefaults | SharedPreferences |

### Pairing (First Connection)

1. User taps "Add Device" and pastes the pairing string from the agent's terminal.
2. App parses the string to extract product ID, device ID, pairing password and SCT.
3. App generates a client keypair (if not already created) and connects to the device.
4. PAKE-based key exchange using the one-time password. Public keys are exchanged.
5. Device stores the client's public key fingerprint; app stores the device bookmark.
6. Device appears in the app's device list.

### Subsequent Connections

1. On the device list, the app connects to saved devices in the background.
2. The control stream (port 2) delivers session lists and prompt lifecycle events. The app displays available tmux sessions per device and renders prompt overlays from server events.
3. User taps a session to enter the terminal view.
4. App sends `POST /terminal/attach` over CoAP (CBOR payload) with session name and current terminal dimensions.
5. App opens a stream via `connection.createStream()` and calls `stream.open()`.
6. Stream relay begins: `stream.readSome()` feeds into the terminal emulator (SwiftTerm on iOS, Termux on Android), terminal key events go to `stream.write()`.

### Connection Lifecycle

```
Client                          Device Agent
  |                                  |
  |---- Connect (Nabto) ------------>|
  |                                  |
  |---- Open ctrl stream (p2) ------>|
  |<--- sessions [p2/cbor] ----------|
  |                                  |
  |---- /terminal/attach [coap] ---->|
  |     {session: "main", cols, rows}|
  |<--- 2.01 Created [coap] ---------|
  |                                  |
  |---- Open data stream (p1) ------>|
  |<--- PTY output [p1/raw] ---------|
  |---- key bytes [p1/raw] --------->|
  |                                  |
  |<--- pattern_present [p2/cbor] ---|
  |<--- pattern_update [p2/cbor] ----|
  |<--- pattern_gone [p2/cbor] ------|
  |---- pattern_resolve [p2/cbor] -->|
  |                                  |
  |---- /terminal/resize [coap] ---->|
  |     {cols: 120, rows: 40}        |
  |<--- 2.04 Changed [coap] ---------|
  |                                  |
  |---- Stream Close ---------------->|
  |---- Connection Close ------------>|
```

- Legend: `[coap]` = CoAP request/response with CBOR payload.
- Legend: `[p2/cbor]` = control-stream framed CBOR message.
- Legend: `[p1/raw]` = raw PTY byte stream.

---

## 7. Prompt Detection System (V2)

### 7.1 Design Goals

1. Detect interactive prompts from terminal UIs using the visible screen state, not historical text tails.
2. Emit stable prompt lifecycle events with no match/dismiss oscillation during redraws.
3. Assign a stable prompt instance identity so redraws of the same prompt do not create new overlays.
4. Keep the iOS and Android apps as renderers/action senders, not detectors.
5. Keep the agent tool-agnostic: behavior is driven by external JSON rules.
6. Guarantee deterministic replay behavior regardless of PTY chunk boundaries.

### 7.2 Architecture Overview

Prompt detection runs on the agent and is evaluated from canonical terminal snapshots.

```text
PTY output (raw bytes)
    |
    v
[VT Parser]
    |
    v
[TerminalState Snapshot]
  - visible lines
  - cursor position
  - viewport size
  - alt-screen flag
    |
    v
[Prompt Rule Evaluator]
    |
    v
[Prompt Lifecycle FSM]
  - instance_id
  - revision
  - present/update/gone
    |
    v
[Control Stream Protocol V2]
    |
    v
[iOS/Android Overlay]
```

### 7.2.1 Essential Decisions

- Prompt detection is server-side only. Clients render and resolve; they do not parse terminal output.
- Pattern config is V3-only and strict. Invalid config is a startup error, not a warning with fallback behavior.
- Detector state is tied to PTY geometry. `POST /terminal/resize` updates both PTY size and detector terminal dimensions.
- Numbered menus are emitted only when the extracted options are complete enough to be actionable:
  - option numbers must start at `1`
  - numbering must be contiguous (`1..N`)
  - at least two actions must be present
- VT parsing must implement redraw-critical control sequences used by modern TUIs, including:
  - charset designation escapes (`ESC ( B`, etc.) which must be consumed and not rendered as text
  - `CSI X` (ECH, Erase Character), which clears characters in-place without moving the cursor
- iOS and Android keep a small `pattern_gone` guard window (minimum visible duration) to avoid overlay flicker if a short transient `gone` slips through.

### 7.3 Canonical Terminal State

`TerminalState` is the only detector input. It is updated incrementally by parsing VT/ANSI control sequences from PTY bytes.

Each snapshot includes:
- visible screen lines (byte-preserving text cells)
- cursor row/column
- viewport dimensions
- active screen mode (main/alt)
- monotonic snapshot sequence

Parser behavior is deterministic and replay-safe:
- line endings normalized to LF
- charset designation control sequences are consumed, not rendered
- erase-in-place operations (`CSI X`) are applied to the backing cell grid

This eliminates stale prompt copies from detection logic because only the current screen is matched.

### 7.4 Prompt Rule Configuration

Rules are loaded from `~/.tmux-remote/patterns/*.json` at startup.

Config schema is V3 only (single format, no compatibility branches):

```json
{
  "version": 3,
  "agents": {
    "claude-code": {
      "name": "Claude Code",
      "rules": [
        {
          "id": "numbered_prompt",
          "type": "numbered_menu",
          "prompt_regex": "Do you want to .+\\?",
          "option_regex": "^\\s*([0-9]+)\\.\\s+(.+)$",
          "action_template": { "keys": "{number}" }
        }
      ]
    }
  }
}
```

Load behavior:
- all `*.json` files in `~/.tmux-remote/patterns/` are loaded in lexical filename order
- agent IDs and pattern IDs must be unique in the merged config
- any parse/validation error aborts startup

Supported prompt types:

| Type | Action Source | Example |
|------|---------------|---------|
| `yes_no` | Static action list from config | `Continue? (y/n)` |
| `accept_reject` | Static action list from config | `Apply changes?` |
| `numbered_menu` | Extracted sequential menu items | `1. Yes / 2. No / 3. Cancel` |

### 7.5 Prompt Instance Identity

Each detected candidate prompt is assigned:

`instance_id = hash(pattern_id, pattern_type, prompt, actions, anchor_row)`

Where:
- `anchor_row` is the matched prompt row in the snapshot
- actions preserve order and key mappings

Properties:
- same prompt across redraws -> same `instance_id`
- semantically different prompt -> different `instance_id`

### 7.6 Lifecycle State Machine

Per data stream, prompt state is managed by a strict FSM.

```text
                 +-------------------------------+
                 | same instance redraw/update   |
                 v                               |
none --present--> active(instance_id, rev=1) ----+
                    |                |
                    | resolve        | absent for gone window
                    v                v
                resolved         gone(instance_id)
                    |                |
                    +------> none <--+
```

Rules:
- `present` is emitted once per new `instance_id`
- `update` is emitted only for same `instance_id` with changed prompt/actions
- `gone` is emitted once when instance is absent for `TMUXREMOTE_PROMPT_ABSENCE_SNAPSHOTS` snapshots (`8`)
- client resolve suppresses only that exact `instance_id`

### 7.7 Control Stream Protocol V2

**Port:** 2  
**Transport:** Nabto stream (not CoAP)  
**Framing:** `[4-byte big-endian uint32 payload length][CBOR payload]`

#### Messages: Agent to Client

**Pattern Present:**
```cbor
{
  "type": "pattern_present",
  "instance_id": "8f0d63c1e4...",
  "revision": 1,
  "pattern_id": "numbered_prompt",
  "pattern_type": "numbered_menu",
  "prompt": "Do you want to create bar.txt?",
  "actions": [
    {"label": "Yes", "keys": "1"},
    {"label": "Yes, allow all", "keys": "2"},
    {"label": "No", "keys": "3"}
  ]
}
```

**Pattern Update:**
```cbor
{
  "type": "pattern_update",
  "instance_id": "8f0d63c1e4...",
  "revision": 2,
  "prompt": "Do you want to create bar.txt?",
  "actions": [
    {"label": "Yes", "keys": "1"},
    {"label": "Yes, allow all", "keys": "2"},
    {"label": "No", "keys": "3"},
    {"label": "Cancel", "keys": "4"}
  ]
}
```

**Pattern Gone:**
```cbor
{
  "type": "pattern_gone",
  "instance_id": "8f0d63c1e4..."
}
```

**Sessions** (unchanged):
```cbor
{
  "type": "sessions",
  "sessions": [
    {"name": "main", "cols": 80, "rows": 24, "attached": 1}
  ]
}
```

#### Messages: Client to Agent

**Pattern Resolve** (action tap or dismiss):
```cbor
{
  "type": "pattern_resolve",
  "instance_id": "8f0d63c1e4...",
  "decision": "action",
  "keys": "1"
}
```

`decision: "dismiss"` omits `keys`.

### 7.8 Mobile Client Integration (iOS and Android)

The integration path is identical on both platforms. Class names are the same; only the language differs (Swift vs Kotlin).

Server event path:

```text
Control stream (port 2, framed CBOR)
    |
    v
ConnectionManager.controlReadLoop()
    |
    v
decode V2 event (present/update/gone)
    |
    v
PatternEngine.applyServerEvent(instance_id,...)
    |
    v
PatternOverlayView / PatternOverlaySheet state update
```

User action path:

```text
User taps action/dismiss
    |
    +--> write keys to data stream (port 1, action only)
    |
    +--> send pattern_resolve(instance_id, decision, keys?) on control stream (port 2, framed CBOR)
```

Both clients apply a short minimum-visible guard on `pattern_gone` to hide brief transients.

### 7.9 Determinism and Replay Guarantees

The detector must satisfy:

1. Same PTY byte stream -> same prompt event sequence.
2. Event sequence is invariant to PTY chunking.
3. Continuous redraw of one prompt does not emit `gone` then `present` cycles.
4. Resolving one prompt instance does not suppress a future distinct prompt.
5. Numbered-menu events are never emitted with missing option `1`, numbering gaps or charset artifacts in labels.

Replay tests use `.ptyr` recordings (including `pty-log-7/8/9/10`) and chunk-split variants of the same recordings.

### 7.10 Tuning Parameters

| Parameter | Purpose |
|-----------|---------|
| `TMUXREMOTE_PROMPT_ABSENCE_SNAPSHOTS` | Snapshot count before emitting `pattern_gone` |
| `max_scan_lines` (rule config) | How many rows below prompt are scanned for numbered options |
| `TMUXREMOTE_PROMPT_MAX_ACTIONS` | Hard cap for action list size |

### 7.11 Threading Model

#### Agent Threads per Data Stream

| Thread | Function | Role |
|--------|----------|------|
| Setup | `stream_setup_thread` | Spawns PTY, tmux attach, initializes prompt detector |
| PTY Reader | `pty_reader_thread` | Reads PTY fd, feeds detector, writes PTY bytes to Nabto stream |
| Stream Reader | `stream_reader_thread` | Reads Nabto stream, writes to PTY fd |

Prompt detection executes in the PTY reader thread (`tmuxremote_prompt_detector_feed()`). Event callbacks send V2 framed-CBOR messages on the control stream.

#### Agent Threads per Control Stream

| Thread | Function | Role |
|--------|----------|------|
| Monitor | `monitor_thread_func` | Polls tmux sessions, broadcasts session snapshots, syncs active prompt instances to newly connected control streams |
| Reader | `reader_thread_func` | Reads `pattern_resolve` from client and forwards resolution to the owning data-stream detector |

#### Why Two Threads per Data Stream

The PTY relay uses two blocking threads rather than a single async event loop because the Nabto Embedded SDK and POSIX have incompatible async models. The PTY is a file descriptor (async via `select`/`poll`/`kqueue`). The Nabto stream is future-based (async via `nabto_device_future_wait` or `nabto_device_future_set_callback`), with no exposed file descriptor. There is no SDK API to add external fds to the Nabto event loop and no way to obtain a pollable fd from a Nabto stream. A single-threaded bridge would require a signaling pipe between the Nabto callback and a `poll` loop, adding complexity with no practical benefit. Two blocking threads, one per source, is the simplest correct approach.

#### Critical Rule: No Blocking in Nabto Callbacks

All Nabto SDK callbacks execute on the SDK core event loop thread. Blocking work is deferred to dedicated threads.
