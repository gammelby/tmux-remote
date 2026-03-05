# CLAUDE.md

Read DESIGN.md before starting any work. It is the source of truth for architecture, protocol, security model, and design decisions.

## Project overview

TmuxRemote is a secure remote terminal tool using Nabto Edge P2P connectivity. It exposes tmux sessions on a machine to authenticated clients without SSH, port forwarding, or firewall configuration.

The repo contains three components that share no source code:

- `agent/` -- C, Nabto Embedded SDK (device side, serves terminal sessions, pattern detection)
- `clients/cli/` -- C, Nabto Client SDK binary (client side, connects to agent)
- `clients/ios/` -- Swift, NabtoEdgeClientSwift (client side, mobile terminal with pattern overlay UI)

## Build

### Agent

```bash
cd agent && make
# or manually: cmake -B _build/$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]') -G Ninja && cmake --build _build/...
```

The Nabto Embedded SDK is fetched automatically via CMake FetchContent. No submodule init needed.

Build artifacts go in `_build/<arch>-<os>/` (e.g. `_build/arm64-darwin/`, `_build/x86_64-linux/`) so host and devcontainer builds coexist.

### CLI client

```bash
cd clients/cli && make
```

The Nabto Client SDK is fetched automatically via CMake FetchContent. No external downloads needed.

### iOS app

Open `clients/ios/TmuxRemote.xcodeproj` in Xcode. NabtoEdgeClientSwift is managed via CocoaPods.

## Architecture rules

- The agent uses the **Nabto Embedded SDK** (open source, C). The clients use the **Nabto Client SDK** (binary release, C API). These are different SDKs. Do not mix them.
- The agent is a generic PTY relay. It knows nothing about what runs in the terminal. Never add agent-side logic that depends on Claude Code, Codex, Aider, or any specific CLI tool. Pattern definitions are external JSON, not compiled in.
- Pattern detection runs on the agent side via a pipeline (ANSI stripper, rolling buffer, PCRE2 regex matcher, pattern engine). Matches are pushed to clients via the control stream (port 2) as CBOR events. The iOS client is a pure display layer for pattern events. See DESIGN.md section 7.
- Communication: Nabto Streams for terminal data (port 1, raw bidirectional byte pipe) and control events (port 2, length-prefixed CBOR). CoAP for control messages (resize, session management). See DESIGN.md section 3.

## Security model

This is critical. TmuxRemote grants remote shell access. A compromise means arbitrary command execution.

- Single IAM role: Owner. Full access or no access. Do not add "limited" roles.
- Password Invite Pairing only. Pairing is closed after each invitation is consumed. No open pairing modes in normal operation. `--demo-init` is the only exception and must print a warning.
- Every CoAP endpoint and stream handler must call `nm_iam_check_access()` before processing. No exceptions.
- See DESIGN.md section 4 for full details.

## Code style

- Agent and CLI client: C (not C++). Match the style of the Nabto Embedded SDK examples.
- iOS app: Swift, UIKit or SwiftUI.
- Use CBOR (content format 60) for all CoAP payloads. Use tinycbor on the C side.
- No em dashes in comments, docs, or output strings. Use colons, semicolons, commas, or separate sentences.

## CoAP endpoints (agent)

All endpoints require IAM authorization except pairing.

| Method | Path | Payload | Response | Purpose |
|--------|------|---------|----------|---------|
| POST | /terminal/resize | `{cols, rows}` | 2.04 | Set PTY size, send SIGWINCH |
| GET | /terminal/sessions | -- | 2.05 `[{name, cols, rows, attached}]` | List tmux sessions |
| POST | /terminal/attach | `{session, cols, rows, create?}` | 2.01 | Set target session for next stream |
| POST | /terminal/create | `{session, cols?, rows?, command?}` | 2.01 | Create new tmux session |
| GET | /terminal/status | -- | 2.05 `{version, active_sessions, uptime}` | Agent health |

## Stream protocol

Port 1: Raw bytes, no framing. Transparent PTY pipe. Terminal escape sequences are handled by the client (local terminal for CLI, SwiftTerm for iOS).

Port 2: Control stream. Length-prefixed CBOR messages. Carries pattern match/dismiss events (agent to client), session state, and client dismiss messages (client to agent).

## File layout

```
agent/
  CMakeLists.txt
  src/

clients/
  cli/
    CMakeLists.txt
    src/

  ios/
    TmuxRemote.xcodeproj/
    TmuxRemote/

DESIGN.md
CLAUDE.md
```

## Development phase

Phase 1 (agent + CLI client) and Phase 2a (iOS app with pattern overlays) are complete. Current work focuses on polish, testing, and publishing.

## Key references

- DESIGN.md (this repo): full system architecture and design
- Nabto Embedded SDK: https://github.com/nabto/nabto-embedded-sdk
- Nabto Client SDK examples: https://github.com/nabto/nabto-client-sdk-examples
- Nabto tcp_tunnel_device (IAM reference): in the embedded SDK repo under `examples/tcp_tunnel_device/`
- SwiftTerm: https://github.com/migueldeicaza/SwiftTerm
- NabtoEdgeClientSwift: available via CocoaPods

## Nabto SDK rules

### No blocking in callbacks (critical)

The Nabto core runs its own event loop thread. All future callbacks execute on that thread.

- A callback function must never make blocking calls (pthread_join, waitpid, blocking write, contested mutex). This freezes the entire SDK.
- Defer blocking work to a separate thread. See existing patterns: `cleanup_thread_func` in `tmuxremote_stream.c`, worker threads in `tmuxremote_coap_handler.c`.
- This applies to both agent (C) and iOS client (Swift): Nabto SDK callbacks dispatch to the core thread. No synchronous I/O or long computation in these callbacks.

### Shutdown sequence

1. Stop all listeners (`nabto_device_listener_stop()`, non-blocking)
2. Call `nabto_device_close()` then `nabto_device_stop()` (blocks until outstanding ops complete)
3. Free listeners, futures, then device, in reverse creation order

## Common mistakes to avoid

- Do not use git submodules. The Embedded SDK is fetched via FetchContent.
- Do not add CC-specific logic to the agent. The agent is tool-agnostic.
- Do not enable open pairing modes by default. Security posture: low probability, catastrophic impact.
- Do not skip IAM checks on any endpoint. Even GET /terminal/status requires authentication.
- Do not frame the stream data. It is a raw byte pipe, same as SSH.

## Writing replay tests from PTY recordings

When a bug is reported with a PTY recording (`agent/tests/fixtures/*.ptyr`), follow this procedure:

1. The reporter provides numbered steps describing what they did and what went wrong. Always include these steps verbatim as a comment at the top of the test.
2. Run a baseline replay (no injected actions) to understand the event timeline.
3. Map the reporter's steps to positions in the recording timeline: identify where user actions (consume, dismiss) should be injected and what the expected outcome is.
4. Write the test: replay the recording, inject actions at the identified positions, assert the expected outcome.
5. Verify the test fails without the fix (reproduces the bug) and passes with the fix.

Rules:
- Recordings and pattern configs must be checked into `agent/tests/fixtures/`. Tests must never depend on environment variables or paths outside the repo.
- Fixture paths are passed via CMake `target_compile_definitions` (e.g., `TEST_FIXTURES_DIR`), not env vars.
- Every replay test must include the reporter's steps as a comment block so future readers understand the real-world scenario.
- If a synthetic test already exists for the same scenario, verify the replay test covers it, then remove the synthetic test.

# Ground rules for debugging

Never, ever start fixing or say "root cause found!" based on just speculation. Always act as an experienced senior engineer, gathering proof through instrumentation and/or tests: Reproduce problems to the extent possible using integration tests. Add stubs and drivers as necessary, executing as long code paths as possible in attempts to reproduce problems. If not possible to reproduce, tell me so instead of speculating about cause. Explain that you cannot reproduce and your instrumentation is not enough and I need to help. Then suggest where you can add further instrumentation and ask me to run manual test to reproduce the problem to allow you gather the instrumentation output. When you have this test and instrumentation supported insight, THEN you can deduce a root cause and make a plan from fixing. BUT ONLY THEN.
