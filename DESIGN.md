# NabtoShell Design

## 1. System Overview

NabtoShell provides secure remote terminal access to tmux sessions using Nabto Edge peer-to-peer connectivity. It replaces SSH with a zero-configuration model: no port forwarding, firewall rules, or dynamic DNS. A lightweight agent runs on the host machine and relays PTY I/O over encrypted Nabto connections to authenticated clients.

```
 iOS App / CLI Client
       |
  [Nabto Edge P2P / DTLS]
       |
   NabtoShell Agent
       |
   tmux session <-> PTY
```

## 2. Components

| Component | Language | SDK | Purpose |
|-----------|----------|-----|---------|
| `agent/` | C | Nabto Embedded SDK | Serves terminal sessions, relays PTY data, detects patterns |
| `clients/cli/` | C | Nabto Client SDK | Command-line terminal client |
| `clients/ios/` | Swift | NabtoEdgeClientSwift | Mobile terminal with pattern overlay UI |

The agent is tool-agnostic. It knows nothing about what runs inside the terminal. Pattern definitions are loaded from JSON configuration files and evaluated generically.

## 3. Communication Model

### Nabto Streams

| Port | Name | Framing | Purpose |
|------|------|---------|---------|
| 1 | Data stream | None (raw bytes) | Bidirectional PTY relay, identical to SSH |
| 2 | Control stream | Length-prefixed CBOR | Session state, pattern match events, client dismiss |

### CoAP Endpoints

All require IAM authorization. Payloads use CBOR (content format 60).

| Method | Path | Purpose |
|--------|------|---------|
| POST | `/terminal/attach` | Set target tmux session for next stream open |
| POST | `/terminal/resize` | Set PTY size, send SIGWINCH |
| POST | `/terminal/create` | Create a new tmux session |
| GET | `/terminal/sessions` | List tmux sessions |
| GET | `/terminal/status` | Agent health and uptime |

## 4. Security Model

NabtoShell grants remote shell access. A compromise means an attacker can execute arbitrary commands as the user running the agent. This is equivalent to SSH access, and the security posture must match or exceed SSH.

- **Single role: Owner.** Full access or no access. No "limited" roles. A multi-role model would be security theater for a remote shell.
- **Password Invite pairing only.** A one-time password is generated per invitation and closed after use. No other pairing mode is ever enabled in normal operation.
- **Every endpoint checks IAM.** CoAP handlers and stream listeners call `nm_iam_check_access()` before processing. Unpaired connections can only access pairing endpoints.
- **DTLS with ECC.** All traffic is encrypted end-to-end by the Nabto platform. The basestation mediates connection setup but cannot decrypt traffic.

### Security Properties

- **End-to-end encryption**: DTLS with ECC key pairs. The Nabto basestation mediates connection setup (hole punching, relay fallback) but cannot decrypt traffic.
- **Mutual authentication**: After pairing, both client and device authenticate each other via public key fingerprints on every connection. No passwords are transmitted or stored after the initial pairing.
- **Zero exposed ports**: The device agent opens no listening TCP or UDP ports. All connectivity is outbound to the Nabto basestation. No attack surface for port scanners.
- **One-time pairing passwords**: Pairing passwords are single-use. After a client pairs, the password is invalidated on the device. A leaked pairing string that has already been used is worthless.
- **PTY isolation**: The PTY is spawned as the user running the agent. tmux session access is limited to that user's sessions. The agent does not run as root.

### Comparison with SSH

| Property | SSH | NabtoShell |
|----------|-----|------------|
| Key exchange | `ssh-keygen` + copy public key to server | Pairing string (one-time password for PAKE key exchange) |
| Authentication | Public key or password per connection | Public key (after initial pairing) |
| Encryption | AES/ChaCha20 over TCP | DTLS with ECC over UDP |
| Network requirements | Open port 22, firewall rules, possibly dynamic DNS | None. Outbound UDP only. |
| NAT traversal | Requires port forwarding or relay (ngrok, etc.) | Built-in P2P hole punching with relay fallback |

The trust model is identical: both rely on a one-time key exchange followed by public key authentication. NabtoShell's pairing is arguably more user-friendly than SSH's `ssh-copy-id` workflow, while providing the same cryptographic guarantees.

### Pairing Flow

The pairing flow is identical across all clients. The transport is the same (Nabto Client SDK); only the UI differs.

1. On `--init`, the agent pre-creates an initial user with a generated one-time password and SCT.
2. The agent prints the pairing string to stdout on startup.
3. The user copies it to their client (CLI command or mobile app paste).
4. The client parses the pairing string, generates a keypair if needed, connects, and completes the PAKE-based key exchange. Public keys are exchanged and stored.
5. The invitation is consumed. Pairing is now closed. No further clients can pair.
6. To add another device, run `nabtoshell-agent --add-user <name>` at the server terminal.

### Agent CLI

#### `nabtoshell-agent --init`

Interactive first-time setup:

```
$ nabtoshell-agent --init
No configuration found. Creating initial setup.

Product ID: pr-xxxxxxxx
Device ID:  de-yyyyyyyy

Generated device keypair.
Fingerprint: 08c955a5f7505f16f03bc3e3e0db89ff56ce571e0dd6be153c5bae9174d62ac6

Register this fingerprint in the Nabto Cloud Console before starting.

Configuration written to ~/.nabtoshell/
```

#### Normal startup (no users paired yet)

```
$ nabtoshell-agent

######## NabtoShell ########
# Product ID:     pr-xxxxxxxx
# Device ID:      de-yyyyyyyy
# Fingerprint:    08c955a5...
# Version:        0.1.0
#
#  No users paired yet. Pair your phone by copying
#  this string into the NabtoShell app:
#
#  p=pr-xxxxxxxx,d=de-yyyyyyyy,u=owner,pwd=CbAHaqpKKrhK,sct=TUfe3n3hhhM9
#
#  This is a one-time pairing password. After you pair,
#  it is invalidated and pairing is closed.
#
######## Waiting for pairing... ########
```

#### `nabtoshell-agent --add-user <name>`

Creates a one-time invitation for an additional client:

```
$ nabtoshell-agent --add-user tablet
Created invitation for user 'tablet'.

Copy this string into the NabtoShell app on the new device:

  p=pr-xxxxxxxx,d=de-yyyyyyyy,u=tablet,pwd=xK9mRtYwZp3n,sct=Hf7kL2nQwR4s

This invitation can only be used once. Pairing will close
again after this device pairs.
```

#### `nabtoshell-agent --remove-user <name>`

Revokes access for a paired user or cancels an unused invitation:

```
$ nabtoshell-agent --remove-user tablet
Removed user 'tablet'. Their public key has been deleted.
They will no longer be able to connect.
```

#### `nabtoshell-agent --demo-init`

Convenience mode for demos and testing. Enables Password Open Pairing (shared password, any number of clients). Prints a clear warning:

```
WARNING: Demo mode enables open pairing. Anyone with the pairing
password can gain terminal access. Do not use in production.
```

### Agent Configuration Directory

```
~/.nabtoshell/
  config/device.json          # Product ID, Device ID, server settings
  config/iam_config.json      # IAM policies, roles (static)
  state/iam_state.json        # Paired users, pairing mode state (mutable)
  keys/device.key             # Device private key
  patterns/*.json             # Pattern definitions
```

## 5. CLI Client

A command-line client for connecting to a NabtoShell agent from another computer. Uses the Nabto Client SDK (binary release). The CLI client is the simplest client and serves as the primary driver for developing and testing the agent. It is a transparent byte pipe: connects, opens a Nabto stream, and relays bytes between the stream and the local terminal's stdin/stdout.

### Usage

```
nabtoshell pair <pairing-string>                    # One-time pairing
nabtoshell attach <device-name> [session]            # Attach to existing tmux session
nabtoshell create <device-name> [session] [command]  # Create new session, optionally run command
nabtoshell sessions <device-name>                    # List tmux sessions
nabtoshell devices                                   # List saved devices
nabtoshell rename <device-name> <new-name>           # Rename a saved device
```

Aliases: `a` for `attach`, `c`/`n`/`new`/`new-session` for `create`.

### Attach/Create Flow

1. Look up device bookmark from `~/.nabtoshell-client/` by name.
2. Create a Nabto client connection with stored product ID, device ID, client private key, and SCT.
3. `nabto_client_connection_connect()`.
4. Send `POST /terminal/attach` or `POST /terminal/create` (CoAP) with session name and terminal dimensions from `ioctl(TIOCGWINSZ)`.
5. Open stream on port 1 via `nabto_client_stream_open()`.
6. Set local terminal to raw mode (`cfmakeraw`).
7. Enter relay loop: `select()` on stdin and the Nabto stream fd (or use the async API with futures).
8. Handle `SIGWINCH`: send `POST /terminal/resize` via CoAP when the local terminal is resized.
9. On stream close or EOF, restore terminal and exit.

### Client Configuration

```
~/.nabtoshell-client/
  client.key              # Client private key
  devices.json            # Saved device bookmarks
```

## 6. iOS App

### Pairing (First Connection)

1. User taps "Add Device" and pastes the pairing string from the agent's terminal.
2. App parses the string to extract product ID, device ID, pairing password, and SCT.
3. App generates a client keypair (if not already created) and connects to the device.
4. PAKE-based key exchange using the one-time password. Public keys are exchanged.
5. Device stores the client's public key fingerprint; app stores the device bookmark.
6. Device appears in the app's device list.

### Subsequent Connections

1. On the device list, the app connects to saved devices in the background.
2. The control stream (port 2) delivers session lists automatically. The app displays available tmux sessions per device.
3. User taps a session to enter the terminal view.
4. App sends `POST /terminal/attach` with session name and current terminal dimensions.
5. App opens a stream via `connection.createStream()` and calls `stream.open()`.
6. Stream relay begins: `stream.readSome()` feeds into SwiftTerm, SwiftTerm key events go to `stream.write()`.

### Connection Lifecycle

```
Client                              Device Agent
  |                                      |
  |---- Connect (Nabto) --------------->|
  |                                      |
  |---- Control Stream Open (port 2) -->|
  |<--- sessions list (CBOR, periodic) -|  (background, every 2s)
  |                                      |
  |  (user selects a session)            |
  |                                      |
  |---- POST /terminal/attach --------->|
  |     {session: "main", cols, rows}    |
  |<--- 2.01 Created -------------------|
  |                                      |
  |---- Data Stream Open (port 1) ----->|
  |                                      | <- forkpty() + tmux attach
  |<--- stream data (PTY output) -------|
  |---- stream data (keystrokes) ------>|
  |         ... interactive session ...  |
  |                                      |
  |<--- pattern_match (port 2, CBOR) ---|  (when prompt detected)
  |---- pattern_dismiss (port 2) ------>|  (user tapped action)
  |                                      |
  |---- POST /terminal/resize --------->|  (on device rotation)
  |     {cols: 120, rows: 40}            |
  |<--- 2.04 Changed -------------------|
  |                                      |
  |---- Stream Close ------------------>|
  |---- Connection Close --------------->|
```

---

## 7. Pattern Recognition System

### 7.1 Design Goals

1. Detect interactive prompts from CLI tools (Claude Code, Aider, Codex) running inside the terminal.
2. Present mobile-friendly overlay buttons so the user can respond without typing.
3. Handle the complexity of TUI frameworks that redraw the entire screen on every update.
4. Keep the agent tool-agnostic: pattern definitions are external JSON, not compiled in.
5. Minimize control stream traffic: one match event when a prompt appears, one dismiss when it leaves. No cycling.

### 7.2 Architecture Overview

Pattern detection runs on the **agent side**, not the client. The agent processes raw PTY output through a multi-stage pipeline that strips ANSI escape sequences, buffers plain text, and evaluates regex patterns. When a match is found, the agent encodes the match (with resolved action buttons) as CBOR and pushes it to the iOS client via the control stream. The client displays an overlay and, when the user taps a button, sends the keystroke to the data stream and a dismiss message back to the agent on the control stream.

```
PTY output (raw bytes)
    |
    v
[ANSI Stripper] -- removes escape sequences, preserves layout
    |
    v
[Rolling Buffer] -- 8192-char circular buffer with total_appended counter
    |
    v
[Pattern Matcher] -- PCRE2 regex, action extraction
    |
    v
[Pattern Engine] -- match lifecycle, auto-dismiss, upgrade, dismiss cooldown
    |
    v
[Control Stream] -- CBOR events to/from iOS client
    |
    v
[iOS Overlay] -- PatternOverlayView with action buttons
```

The iOS client receives pre-resolved matches from the agent and displays them. It contains no client-side pattern detection logic.

### 7.3 Pattern Configuration

Patterns are defined in JSON files loaded from `~/.nabtoshell/patterns/` at agent startup. Each file defines one or more agents, each with an ordered list of patterns.

```json
{
  "version": 2,
  "agents": {
    "claude-code": {
      "name": "Claude Code",
      "patterns": [
        {
          "id": "numbered_prompt",
          "type": "numbered_menu",
          "regex": "Do you want to .+\\?\\n.*1\\. .+\\n.*2\\. .+",
          "multi_line": true,
          "action_template": { "keys": "{number}" }
        },
        {
          "id": "yes_no_prompt",
          "type": "yes_no",
          "regex": "(?:Allow|Proceed).*\\? \\(y/n\\)",
          "actions": [
            { "label": "Allow", "keys": "y" },
            { "label": "Deny", "keys": "n" }
          ]
        }
      ]
    }
  }
}
```

**Pattern types:**

| Type | Actions | Use case |
|------|---------|----------|
| `yes_no` | Static from config | "Continue? (y/n)" prompts |
| `accept_reject` | Static from config | "Accept changes?" prompts |
| `numbered_menu` | Extracted dynamically | "1. Yes / 2. No / 3. Cancel" menus |

For `numbered_menu`, `action_template.keys` contains `{number}` which is substituted with the item number (e.g., `{number}` becomes `1`, `2`, `3`).

### 7.4 Agent Pipeline: Stage by Stage

#### Stage 1: ANSI Stripper

**Files:** `nabtoshell_ansi_stripper.h`, `nabtoshell_ansi_stripper.c`

A byte-level state machine that removes terminal escape sequences while preserving text layout for regex matching. Six states:

| State | Trigger | Behavior |
|-------|---------|----------|
| `GROUND` | Default | Pass through printable characters |
| `ESCAPE` | `0x1B` (ESC) | Await command byte |
| `ESCAPE_INTERMEDIATE` | ESC + byte in 0x20-0x2F | Consume until final byte (handles `ESC(B` charset select, which is 3 bytes not 2) |
| `CSI` | `ESC [` | Consume parameter bytes; on final byte, emit whitespace for cursor movement |
| `OSC` | `ESC ]` | Consume until `ESC \` or BEL (0x07) |
| `OSC_ESCAPE` | ESC inside OSC | Await `\` to terminate OSC (two-byte `ESC \` terminator) |

**Cursor movement to whitespace conversion:**

CSI sequences that move the cursor indicate spacing in TUI frameworks like ink/React (used by Claude Code). Instead of emitting actual space characters, these TUIs use `ESC[nC` (cursor forward) between words and `ESC[row;colH` (cursor position) for line breaks.

| CSI Final Byte | Meaning | Emitted |
|---------------|---------|---------|
| C (0x43) | Cursor Forward | Space |
| G (0x47) | Cursor Horizontal Absolute | Space |
| A, B, E, F, H, d, f | Vertical movement / Cursor Position | Newline |

**UTF-8 cross-chunk handling:**

Terminal data arrives in fixed-size chunks (typically 1024 or 4096 bytes) that can split multi-byte UTF-8 characters. For example, the heavy right-pointing angle `U+276F` is encoded as `E2 9D AF`; a chunk boundary may fall between `E2` and `9D`.

The stripper maintains a `pendingUTF8[4]` buffer. When a chunk ends with an incomplete UTF-8 lead byte, the partial bytes are saved and prepended to the next chunk's output.

**Other transformations:**
- TAB (0x09): replaced with 4 spaces
- Control characters (0x00-0x08, 0x0B-0x0C, 0x0E-0x1F): stripped
- CR (0x0D): normalized to LF (line breaks use LF)

#### Stage 2: Rolling Buffer

**Files:** `nabtoshell_rolling_buffer.h`, `nabtoshell_rolling_buffer.c`

An 8192-byte circular buffer that retains the most recent terminal text. When appending would exceed capacity, the oldest bytes are discarded. The buffer skips leading UTF-8 continuation bytes on overflow to avoid creating invalid sequences.

Key field: `total_appended` is a monotonically increasing counter of all bytes ever appended. It is used by the pattern engine to compute match age and dismiss thresholds. It never resets during a session.

#### Stage 3: Pattern Matcher

**Files:** `nabtoshell_pattern_matcher.h`, `nabtoshell_pattern_matcher.c`

Compiles pattern definitions into PCRE2 regex objects and evaluates them against buffer text. Patterns are evaluated in config order; the first match with resolvable actions wins.

**Regex compilation flags:**
- Always: `PCRE2_UTF`
- If `multi_line: true`: `PCRE2_MULTILINE | PCRE2_DOTALL`

**Action resolution by type:**

For `yes_no` and `accept_reject`, actions are copied directly from the pattern definition.

For `numbered_menu`, a two-pass extraction is used:

**Pass 1: Find the last "1." item.** The regex `(\d+)\.\s+(.+)` is applied repeatedly across the entire text-from-match region. Every occurrence of item number 1 has its offset recorded. The last one wins. This is critical because TUI redraws leave multiple prompt copies in the buffer, and the `DOTALL` main regex matches the first (oldest, possibly incomplete) copy.

**Pass 2: Extract sequential items.** Starting from the last "1." offset, items are extracted in strict sequential order (1, 2, 3, ...). Extraction stops when a non-sequential number is encountered or the text ends. For each item, the `{number}` placeholder in the action template is substituted with the actual number.

**Selection indicator stripping:** Before checking if a line starts with a numbered item, leading indicators are stripped: `>`, `*`, `-`, and `U+276F` (heavy right-pointing angle quotation mark, used by Claude Code as the selection cursor).

**Prompt extraction:** For numbered menus, the prompt text (e.g., "Do you want to create bar.txt?") is extracted by scanning the matched text for the last non-empty, non-numbered line before the first item.

#### Stage 4: Pattern Engine

**Files:** `nabtoshell_pattern_engine.h`, `nabtoshell_pattern_engine.c`

The engine orchestrates the full pipeline and manages match lifecycle. It is instantiated per data stream (one per connected client). All state is protected by a `pthread_mutex_t`.

**Constants:**

| Constant | Value | Purpose |
|----------|-------|---------|
| `PATTERN_ENGINE_BUFFER_CAPACITY` | 8192 | Rolling buffer size |
| `PATTERN_ENGINE_MATCH_WINDOW` | 4000 | Bytes scanned for new matches |
| `PATTERN_ENGINE_AUTO_DISMISS` | 1500 | Bytes before auto-dismiss check triggers |

**`feed()` processing order:**

Each call to `feed()` processes one chunk of PTY output:

1. **ANSI strip** (under mutex): raw bytes through the stripper, output into rolling buffer.

2. **Auto-detect agent**: if no agent is selected and config is loaded, scan the last 512 bytes for tool signatures ("Claude Code", "Aider", "Codex") and auto-select the matching agent.

3. **Numbered menu upgrade check**: if the active match is a `numbered_menu` and fewer than `AUTO_DISMISS` chars have arrived since the match, rescan a small window (`chars_since_match + 500` chars, not the full `MATCH_WINDOW`) for the same pattern. If the rescan finds more actions (items that arrived in a later PTY chunk), upgrade the active match and fire a callback. The small window ensures the regex finds the latest prompt copy, not an older partial one still in the buffer.

4. **Auto-dismiss check**: if the active match is older than `AUTO_DISMISS` chars, rescan a small window (`chars_since_match + 500`). If the same pattern is still present (TUI redraw), reset the match age (no callback). If the pattern is gone (user navigated away), dismiss (one callback). The small scan window, combined with the two-pass item extraction, ensures TUI redraws with multiple buffer copies are handled correctly.

5. **Dismiss cooldown check**: if the match was dismissed, check if enough new content has arrived to clear the cooldown. User-initiated dismiss requires `MATCH_WINDOW` (4000) chars to ensure the old prompt text has scrolled out. Auto-dismiss requires only `AUTO_DISMISS` (1500) chars since the prompt is already gone.

6. **New match search**: if no active match and not in cooldown, scan the last `MATCH_WINDOW` bytes for a new pattern match.

**Callback mechanism:** When match state changes (new match, dismiss, or upgrade), the engine calls `on_change(match, user_data)` outside the mutex. The data stream's callback encodes the match as CBOR and sends it via the control stream.

**Agent selection:** `select_agent()` loads the agent's patterns into the matcher and re-evaluates the existing buffer. This allows late agent selection (e.g., after auto-detection) to immediately find already-visible prompts.

**User dismiss:** `dismiss()` sets `dismissed = true`, `user_dismissed = true`, and records `dismissed_at_position`. This is called when the client sends a `pattern_dismiss` message back via the control stream.

### 7.5 Control Stream Protocol

**Port:** 2 (separate Nabto stream from the data port)

**Framing:** `[4-byte big-endian uint32 payload length][CBOR payload]`

#### Messages: Agent to Client

**Pattern Match:**
```cbor
{
  "type": "pattern_match",
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

**Pattern Dismiss:**
```cbor
{ "type": "pattern_dismiss" }
```

**Sessions** (polled every 2 seconds):
```cbor
{
  "type": "sessions",
  "sessions": [
    {"name": "main", "cols": 80, "rows": 24, "attached": true}
  ]
}
```

#### Messages: Client to Agent

**Pattern Dismiss** (user tapped an action or the dismiss button):
```cbor
{ "type": "pattern_dismiss" }
```

A dedicated reader thread per control stream reads these messages. On `pattern_dismiss`, it looks up the data stream by `connectionRef` and calls `nabtoshell_pattern_engine_dismiss()`.

### 7.6 iOS Client Integration

#### Server Event Path (production)

```
Control stream (port 2)
    |
    v
ConnectionManager.controlReadLoop()
    -- reads [4-byte length][CBOR], decodes via CBORHelpers
    |
    v
onPatternEvent callback
    -- set in TerminalScreen.setupCallbacks()
    -- filters by deviceId
    |
    v
PatternEngine.applyServerMatch() / applyServerDismiss()
    -- respects activeAgent guard (no overlay if agent pill is off)
    -- 2-second debounce after user dismiss to prevent oscillation
    |
    v
@Observable activeMatch property
    -- drives PatternOverlayView visibility via SwiftUI
```

#### User Action Path

```
User taps action button in PatternOverlayView
    |
    v
onAction callback in TerminalScreen
    |
    +---> nabtoService.writeToStream(action.keys)   [keystroke to PTY via port 1]
    +---> patternEngine.dismiss()                     [clear overlay, set debounce]
    +---> connectionManager.sendPatternDismiss()      [CBOR dismiss to agent via port 2]
```

#### Stub Testing

In `#if DEBUG` builds with `StubNabtoService`, scripted pattern events (match/dismiss) are delivered directly to the `ConnectionManager.onPatternEvent` callback, matching the production code path. No client-side pattern detection logic is needed. Stub scripts define the exact events the agent would push, allowing UI tests to run without a real agent or network.

#### Agent Selection

The iOS app shows an "Agent" pill in the terminal toolbar. The user can select an agent (Claude Code, Aider, etc.) or turn detection off. Selection is persisted in `UserDefaults` per device ID and restored on reconnect.

### 7.7 Pattern Overlay UI

**File:** `PatternOverlayView.swift`

A bottom sheet anchored to the terminal view. Rendered differently per pattern type:

- **yes_no / accept_reject:** Full-width buttons, first option bold, "No"/"Deny"/"Reject" in red.
- **numbered_menu:** Left-aligned label list, scrollable if more than 5 items. "No" items in red.
- **All types:** "Dismiss" button at the bottom to close without acting.

The overlay disables hit testing on the terminal view underneath (`allowsHitTesting(patternEngine.activeMatch == nil)`), so the user must interact with the overlay or dismiss it.

### 7.8 Tuning Constants and Rationale

| Constant | Value | Why |
|----------|-------|-----|
| Buffer capacity (8192) | Holds ~4 full TUI screen redraws | Enough history for the match window plus context for auto-detect |
| Match window (4000) | ~2 screen redraws | Large enough to catch prompts that appear mid-screen, small enough that old prompts eventually scroll out |
| Auto-dismiss threshold (1500) | ~0.75 screen redraws | Fast enough to detect when a prompt disappears, slow enough to not re-trigger on every feed |
| Upgrade scan window (chars_since + 500) | Just the recent text | Avoids matching older partial prompt copies in the buffer |
| User dismiss cooldown (4000 = MATCH_WINDOW) | Full window flush | Ensures the dismissed prompt text has completely left the match window before re-scanning |
| Auto dismiss cooldown (1500 = AUTO_DISMISS) | Shorter | The prompt already left the window, so less cooldown needed |
| Server match debounce (2 seconds) | Calendar time on client | After user taps, suppresses agent-side re-matches during the brief period when the TUI is redrawing the new prompt |

### 7.9 Key Design Decisions

**1. Agent-side detection, not client-side.**
The agent has access to the raw PTY output before it is sent to the client. Running detection on the agent means the iOS client does not need to bundle PCRE2 or implement a pattern matching pipeline. Pattern configs are managed in one place, and the control stream carries pre-resolved action buttons. The iOS client is a pure display layer for pattern events.

**2. Per-stream pattern engine.**
Each data stream (PTY connection) gets its own engine instance. This avoids interference between simultaneous sessions and simplifies lifetime management. The engine is initialized in the stream setup thread and freed when the stream closes.

**3. Small scan windows for rescan operations.**
TUI frameworks redraw the entire screen on every update, leaving multiple copies of the same prompt in the rolling buffer. With `PCRE2_DOTALL`, the regex `Do you want to .+\?\n.*1\. .+\n.*2\. .+` matches greedily from the FIRST prompt copy (oldest, possibly truncated) through the second. Using a small scan window (`chars_since_match + 500`) for upgrade checks and auto-dismiss rescans ensures the regex finds only the latest prompt copy. This is the core insight that prevents action count degradation.

**4. Two-pass numbered item extraction.**
Even with small scan windows, `extract_menu_items` uses a two-pass approach: first find the offset of the LAST "1." item in the text, then extract sequentially from there. This handles edge cases where filler text between prompt copies contains digit sequences.

**5. Bidirectional control stream for dismiss.**
When the user taps an action, the keystroke goes to the PTY (port 1) and a dismiss message goes to the agent (port 2). Without this, the agent would see the TUI redraw (new prompt with same text) and keep the old match alive via age-reset, never sending a new match event for the genuinely new prompt.

**6. Dismiss cooldown with separate thresholds.**
User dismiss requires 4000 chars of cooldown to flush the old prompt text completely from the match window. Auto-dismiss requires only 1500 chars because the prompt has already left the window. This prevents re-triggering on stale text while keeping detection responsive for new prompts.

**7. No force-dismiss.**
An earlier design had a `MAX_MATCH_AGE` that force-dismissed after N chars regardless of whether the prompt was still visible. This caused dismiss/rematch/dismiss cycling through the control stream, flooding the iOS client with events. The current design uses age-reset: if the prompt is still in the scan window during an auto-dismiss check, the match age is simply reset. The match persists as long as the prompt is actively being redrawn.

### 7.10 Threading Model

#### Agent Threads per Data Stream

| Thread | Function | Role |
|--------|----------|------|
| Setup | `stream_setup_thread` | Spawns PTY, tmux attach, initializes pattern engine |
| PTY Reader | `pty_reader_thread` | Reads PTY fd, feeds pattern engine, writes to Nabto stream |
| Stream Reader | `stream_reader_thread` | Reads Nabto stream, writes to PTY fd |

The PTY reader thread is where pattern detection happens. It calls `nabtoshell_pattern_engine_feed()` synchronously for each PTY read. The match callback sends CBOR via the control stream (which has its own `writeMutex`).

#### Agent Threads per Control Stream

| Thread | Function | Role |
|--------|----------|------|
| Monitor | `monitor_thread_func` | Polls tmux sessions, broadcasts state, replays pattern match to new clients |
| Reader | `reader_thread_func` | Reads client-sent dismiss messages, calls engine dismiss |

#### Critical Rule: No Blocking in Nabto Callbacks

All Nabto SDK callbacks execute on the SDK's core event loop thread. Blocking in a callback freezes the entire SDK. All blocking work (PTY I/O, thread joins, contested mutexes) is deferred to dedicated threads.

## 8. File Layout

```
agent/
  src/
    main.c                               # Entry point, argument parsing
    nabtoshell.h / .c                    # Main app struct, startup, shutdown
    nabtoshell_banner.h / .c             # Startup banner output
    nabtoshell_device.h / .c             # Nabto device setup
    nabtoshell_init.h / .c               # --init, --add-user, --remove-user logic
    nabtoshell_stream.h / .c             # Data stream (port 1), PTY relay
    nabtoshell_control_stream.h / .c     # Control stream (port 2), events
    nabtoshell_tmux.h / .c               # tmux session utilities
    nabtoshell_pattern_engine.h / .c     # Match lifecycle, feed pipeline
    nabtoshell_pattern_matcher.h / .c    # PCRE2 regex, action extraction
    nabtoshell_pattern_config.h / .c     # JSON config parsing
    nabtoshell_pattern_cbor.h / .c       # CBOR encode/decode for matches
    nabtoshell_ansi_stripper.h / .c      # Terminal escape removal
    nabtoshell_rolling_buffer.h / .c     # Circular text buffer
    nabtoshell_coap_handler.h / .c       # CoAP endpoint scaffold
    nabtoshell_coap_attach.c             # POST /terminal/attach handler
    nabtoshell_coap_create.c             # POST /terminal/create handler
    nabtoshell_coap_resize.c             # POST /terminal/resize handler
    nabtoshell_coap_sessions.c           # GET /terminal/sessions handler
    nabtoshell_coap_status.c             # GET /terminal/status handler
    nabtoshell_iam.h / .c                # IAM integration
    nabtoshell_session.h / .c            # Session map
  tests/
    test_pattern_engine.c                # Pattern engine pipeline tests
    test_ansi_stripper.c                 # ANSI stripper tests
    test_pattern_matcher.c               # Pattern matcher tests
    test_pattern_config.c                # Config parsing tests
    test_pattern_broadcast.c             # Control stream broadcast tests
    test_pattern_routing.c               # Pattern routing tests

clients/
  cli/
    src/                                 # CLI client (C)
  ios/
    NabtoShell/
      App/
        NabtoShellApp.swift              # App entry point
        RootView.swift                   # Root navigation
        AppState.swift                   # App-wide state
      Models/
        DeviceBookmark.swift             # Device bookmark model
        PairingInfo.swift                # Pairing string parser
        SessionInfo.swift                # Session data model
      Patterns/
        PatternEngine.swift              # Server event handler, agent selection
        PatternMatch.swift               # Match data model
        PatternConfig.swift              # Config structs
        PatternConfigLoader.swift        # Bundle JSON loader
      Services/
        ConnectionManager.swift          # Control stream read/write
        CBORHelpers.swift                # CBOR encoding/decoding
        NabtoService.swift               # Nabto connection management
        BookmarkStore.swift              # Device bookmark persistence
        KeychainService.swift            # Keychain for private keys
        ReconnectLogic.swift             # Auto-reconnect on disconnect
        ResumeLogic.swift                # Session resume on app foreground
        AppLog.swift                     # Debug logging
      Views/
        DeviceListView.swift             # Device list and session selection
        PairingView.swift                # Pairing string input
        WelcomeView.swift                # First-launch welcome
        TerminalScreen.swift             # Main terminal view, pattern integration
        PatternOverlayView.swift         # Action button overlay
        TerminalViewWrapper.swift        # SwiftTerm UIKit bridge
        KeyboardAccessoryView.swift      # Terminal keyboard accessory bar
      Resources/
        patterns.json                    # Bundled pattern definitions

~/.nabtoshell/
  config/device.json                     # Product/device IDs
  state/iam_state.json                   # Paired users
  keys/device.key                        # Device private key
  patterns/*.json                        # Pattern definitions
```
