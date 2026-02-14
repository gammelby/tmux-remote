"""Test that unauthorized access is rejected.

Runs against the persistent test server started with 'make run-test-server'.
"""

import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from helpers import run_cli


def test_attach_without_pairing(cli_binary, client_home):
    """Attaching without pairing should fail (device not in bookmarks)."""
    env = {"TMUX_REMOTE_HOME": client_home}
    result = run_cli(cli_binary, ["attach", "nonexistent"],
                     env_override=env, timeout=10)
    assert result.returncode != 0


def test_sessions_without_pairing(cli_binary, client_home):
    """Listing sessions without pairing should fail (device not in bookmarks)."""
    env = {"TMUX_REMOTE_HOME": client_home}
    result = run_cli(cli_binary, ["sessions", "nonexistent"],
                     env_override=env, timeout=10)
    assert result.returncode != 0
