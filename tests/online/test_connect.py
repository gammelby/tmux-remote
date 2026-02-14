"""Test interactive terminal connection.

Runs against the persistent test server started with 'make run-test-server'.
Uses a pre-paired client from 'make setup-test-client'.
"""

import os
import pty
import select
import subprocess
import time
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from helpers import run_cli


def test_attach_interactive(paired_env, cli_binary):
    """Attach and send a command, verify output appears."""
    env = paired_env

    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        [cli_binary, "attach", "default"],
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        env={**os.environ, **env},
    )

    time.sleep(3)

    os.write(master_fd, b"echo tmuxremote_test_marker\n")
    time.sleep(2)

    output = b""
    while True:
        ready, _, _ = select.select([master_fd], [], [], 0.5)
        if not ready:
            break
        chunk = os.read(master_fd, 4096)
        if not chunk:
            break
        output += chunk

    output_str = output.decode("utf-8", errors="replace")

    os.write(master_fd, b"exit\n")
    time.sleep(1)

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

    os.close(master_fd)
    os.close(slave_fd)

    assert "tmuxremote_test_marker" in output_str, \
        f"Expected marker not found in output: {output_str}"
