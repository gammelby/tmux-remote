"""Test listing tmux sessions on a device.

Runs against the persistent test server started with 'make run-test-server'.
Uses a pre-paired client from 'make setup-test-client'.
"""

import subprocess
import time
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from helpers import run_cli


def test_sessions_lists_known_session(paired_env, cli_binary):
    """After creating a tmux session, 'sessions' should list it."""
    env = paired_env
    session_name = "tmuxremote_test_sess"

    subprocess.run(["tmux", "new-session", "-d", "-s", session_name],
                   check=False)
    time.sleep(1)

    try:
        result = run_cli(cli_binary, ["sessions", "default"],
                         env_override=env)
        output = result.stdout.decode("utf-8", errors="replace")

        assert result.returncode == 0, f"Sessions failed: {output}"
        assert session_name in output, \
            f"Session '{session_name}' not found in: {output}"
    finally:
        subprocess.run(["tmux", "kill-session", "-t", session_name],
                       check=False)
