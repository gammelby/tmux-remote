"""Test client pairing validation.

Runs against the persistent test server started with 'make run-test-server'.
Only includes tests that do not consume a pairing invitation.
"""

import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from helpers import run_cli


def test_pair_invalid_string(cli_binary, client_home):
    """Pairing with a malformed string fails."""
    env = {"TMUX_REMOTE_HOME": client_home}

    result = run_cli(cli_binary, ["pair", "invalid-string"], env_override=env)
    assert result.returncode != 0
