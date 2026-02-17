# tmux-remote

Secure remote terminal access over Nabto Edge P2P connectivity. Exposes tmux sessions on a machine to authenticated clients without SSH, port forwarding, or firewall configuration.

All traffic is end-to-end encrypted (DTLS with ECC). No ports are opened on the agent machine; connectivity is established through the Nabto basestation which mediates P2P connection setup but never sees your data.

## Prerequisites

- CMake 3.14+, Ninja (or Make)
- tmux installed on the agent machine
- A device identity from the [Nabto Cloud Console](https://console.cloud.nabto.com/) (Product ID + Device ID)
- Python 3 and pytest for integration tests

## Building

### Quick build (both components)

```bash
make
```

### Agent only

The Nabto Embedded SDK is fetched automatically via CMake FetchContent.

```bash
make agent
# Binary: agent/_build/tmux-remote-agent
```

### CLI client

The Nabto Client SDK is fetched automatically via CMake FetchContent (downloaded once at configure time).

```bash
make client
# Binary: clients/cli/_build/tmux-remote
```

## Usage

### Overview

tmux-remote has two components:

- **Agent** (`tmux-remote-agent`): runs on the machine you want to access remotely. Serves tmux sessions over Nabto.
- **CLI client** (`tmux-remote`): runs on the machine you are working from. Connects to a remote agent and opens an interactive terminal.

The typical workflow is: initialize the agent, pair a client, then attach to a session.

### Step 1: Initialize the agent

On the machine you want to access remotely, create a device identity in the [Nabto Cloud Console](https://console.cloud.nabto.com/) and initialize the agent:

```bash
tmux-remote-agent --init -p pr-xxxxxxxx -d de-yyyyyyyy
```

This creates the configuration in `~/.tmux-remote/` and prints:

```
Device fingerprint: a1b2c3d4e5f6...

Pairing string for the first client:
p=pr-xxxxxxxx,d=de-yyyyyyyy,u=owner,pwd=CbAHaqpKKrhK,sct=TUfe3n3hhhM9
```

Copy the device fingerprint and register it in the Nabto Cloud Console for your device.

The pairing string is a one-time credential. After a client uses it, it is invalidated.

### Optional: Move device key storage backend

If you want to move an existing device private key between filesystem and macOS Keychain storage, use:

```bash
tmux-remote-agent --move-device-key keychain
```

or:

```bash
tmux-remote-agent --move-device-key filesystem
```

This is a migration operation, not just a lookup preference. The agent moves the same key, verifies the fingerprint, updates configuration and then removes the old copy when possible.

### Step 2: Start the agent

```bash
tmux-remote-agent
```

The agent attaches to the Nabto basestation and waits for connections. It prints a banner showing paired users and available tmux sessions.

Make sure at least one tmux session exists (the agent relays existing tmux sessions; it does not create them on its own):

```bash
tmux new-session -d -s main
```

### Step 3: Pair the client

On the machine you will connect from, run the pair command with the pairing string from Step 1:

```bash
tmux-remote pair p=pr-xxxxxxxx,d=de-yyyyyyyy,u=owner,pwd=CbAHaqpKKrhK,sct=TUfe3n3hhhM9 \
  --name my-server
```

This exchanges public keys with the agent and saves a device bookmark. The `--name` flag sets a friendly name used in subsequent commands. If omitted, the device ID is used.

Pairing is a one-time operation. All future connections authenticate with the exchanged keys.

### Step 4: Attach to a session

```bash
tmux-remote attach my-server
```

This opens an interactive terminal attached to the default tmux session ("main") on the remote machine. The session behaves like SSH: keystrokes are sent to the remote PTY, and output is displayed locally. Terminal resize events are forwarded automatically.

Press `Ctrl-C` or type `exit` to disconnect.

### Listing sessions

```bash
tmux-remote sessions my-server
```

Output:

```
Sessions on 'my-server':
  NAME             SIZE         STATUS
  main             200x50       (attached)
  dev              120x40
  background       80x24
```

### Attaching to a specific session

```bash
tmux-remote attach my-server dev
```

If the session does not exist, the command fails with an error.

### Creating a new session

```bash
tmux-remote create my-server work
```

This creates a tmux session named "work" on the remote machine and attaches to it. If the session name is omitted, a name is auto-generated.

To run a specific command instead of the default shell:

```bash
tmux-remote create my-server claude --command "claude --resume"
```

Short aliases: `tmux-remote n`, `tmux-remote new`, `tmux-remote c`.

### Listing paired devices

```bash
tmux-remote devices
```

Shows all saved device bookmarks with their names, product IDs, and device IDs.

### Client home directory

Client state (private key and device bookmarks) is stored in `~/.tmux-remote-client/`. Override this with the `TMUX_REMOTE_HOME` environment variable:

```bash
TMUX_REMOTE_HOME=/tmp/alt-client tmux-remote devices
```

### Adding more clients

Each pairing invitation is single-use. To pair a second device (phone, laptop, etc.), create a new invitation on the agent machine:

```bash
tmux-remote-agent --add-user tablet
```

This prints a new pairing string. Use it to pair the second client:

```bash
tmux-remote pair <new-pairing-string> --name my-server
```

### Revoking access

```bash
tmux-remote-agent --remove-user tablet
```

The revoked client can no longer connect.

## Multiple Sessions and Clients

A single agent (one device ID) supports up to 8 concurrent connections. Multiple clients can connect simultaneously, each to a different tmux session or to the same one.

### How it works

When a client connects, it tells the agent which tmux session to attach to (via a CoAP control message). The agent then opens a PTY running `tmux attach-session -t <name>` and relays it over a Nabto stream. Each connection gets its own independent PTY, reader thread, and stream.

```
Agent (one device ID)
  |
  +-- Client A connects, attaches to "main"
  |     PTY 1 -> tmux attach-session -t main
  |
  +-- Client B connects, attaches to "dev"
  |     PTY 2 -> tmux attach-session -t dev
  |
  +-- Client C connects, attaches to "main"
        PTY 3 -> tmux attach-session -t main
        (tmux handles the multi-attach natively)
```

- **Different sessions**: Alice attaches to "main", Bob attaches to "dev". Each gets an independent terminal.
- **Same session**: Alice and Bob both attach to "main". tmux multiplexes them: both see the same output and either can type. This is standard tmux shared-session behavior.
- **Terminal resize**: Each client's resize events only affect its own PTY. If Alice resizes her terminal, Bob's is unaffected.

No extra device IDs, agent instances, or configuration are needed for multiple sessions or clients.

## Agent Reference

```
tmux-remote-agent [options]

Options:
  -h, --help                Show help
  -v, --version             Show version
  -H, --home-dir <dir>      Home directory (default: ~/.tmux-remote/)
      --init                Initialize configuration
      --demo-init           Removed (invite-only pairing enforced)
      --add-user <name>     Create a pairing invitation for a new user
      --remove-user <name>  Revoke access for a user
      --move-device-key <filesystem|keychain>
                            Move device private key between storage backends
  -p, --product-id <id>     Product ID (used with --init)
  -d, --device-id <id>      Device ID (used with --init)
      --log-level <level>   Log level (error|info|trace|debug)
      --random-ports        Use random ports
      --record-pty <path>   Record raw PTY data to file
```

## CLI Client Reference

```
tmux-remote <command> [options]

Commands:
  pair <pairing-string>           One-time pairing with a device
    --name <name>                 Friendly name for the device bookmark

  attach <device> [session]       Attach to an existing tmux session (alias: a)
                                  Default session: "main"

  create <device> [session]       Create a new session and attach (aliases: new, n, c)
    --command <cmd>               Run a specific command instead of the default shell

  sessions <device>               List tmux sessions on a device

  devices                         List saved device bookmarks

  rename <current> <new>          Rename a device bookmark

  --help, -h                      Show help
  --version, -v                   Show version
```

## Security Model

tmux-remote grants remote shell access. The security model reflects this: a compromise means arbitrary command execution.

- **Single role**: Owner. Full access or no access. There are no "limited" or "read-only" roles.
- **Password-invite pairing only**: Each pairing invitation is single-use. After a client pairs, the password is invalidated and pairing is closed. No open pairing modes in normal operation.
- **Key-based authentication**: After the initial pairing, all connections authenticate with exchanged public keys. No passwords are stored or transmitted.
- **End-to-end encryption**: All traffic uses DTLS with ECC. The Nabto basestation never sees plaintext.
- **IAM enforcement**: Every CoAP endpoint and stream handler calls `nm_iam_check_access()` before processing. Unpaired clients can only reach the pairing endpoint.
- **No open ports**: The agent makes outbound connections to the Nabto basestation. No listening sockets are opened on the agent machine.

## Running Tests

Integration tests require a running agent with real Nabto Cloud credentials.

### Setup

```bash
# Initialize test server
make init-test-server PRODUCT_ID=pr-xxx DEVICE_ID=de-xxx
# Register the printed fingerprint in the Nabto Cloud Console

# Terminal 1: start the agent
make run-test-server

# Terminal 2: pair the test client (one-time)
make setup-test-client
```

### Run tests

```bash
# Offline tests (agent CLI flags, no server needed)
make run-tests-offline

# Online tests (requires running server + paired client)
make run-tests-online

# Both suites sequentially
make run-test-client-suite
```

If your `python3` does not have pytest, specify the path:

```bash
make run-tests-online PYTHON=.venv/bin/python3
```

## Architecture

See SPEC.md for the full product specification and CLAUDE.md for development instructions.
