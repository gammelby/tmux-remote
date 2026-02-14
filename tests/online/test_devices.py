"""Test device bookmark listing.

Runs against the persistent test server started with 'make run-test-server'.
"""

import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from helpers import run_cli


def test_devices_empty(cli_binary, client_home):
    """With no paired devices, 'devices' shows empty list."""
    env = {"TMUX_REMOTE_HOME": client_home}
    result = run_cli(cli_binary, ["devices"], env_override=env)
    output = result.stdout.decode("utf-8", errors="replace")

    assert result.returncode == 0, f"Devices failed: {output}"
    assert "no saved" in output.lower() or "no device" in output.lower()


def test_devices_after_pairing(paired_env, cli_binary, product_id, device_id):
    """With a pre-paired client, 'devices' lists the paired device."""
    env = paired_env

    dev_result = run_cli(cli_binary, ["devices"], env_override=env)
    dev_output = dev_result.stdout.decode("utf-8", errors="replace")

    assert dev_result.returncode == 0
    assert device_id in dev_output or product_id in dev_output, \
        f"Device not found in output: {dev_output}"
