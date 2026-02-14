import os
import shutil
import pytest


@pytest.fixture(scope="session")
def paired_client_home(config):
    """Pre-paired client home from 'make setup-test-client'."""
    path = config.get("client_home_dir", "")
    if not path:
        pytest.skip("No client_home_dir in test_config.json")
    path = os.path.abspath(path)
    if not os.path.isdir(path):
        pytest.skip(
            f"Pre-paired client home not found at {path}. "
            "Run: make setup-test-client"
        )
    # Verify it actually has device state
    devices_file = os.path.join(path, "devices.json")
    if not os.path.exists(devices_file):
        pytest.skip(
            f"No devices.json in {path}. "
            "Run: make setup-test-client"
        )
    return path


@pytest.fixture(scope="session")
def paired_env(paired_client_home):
    """Environment dict pointing at the pre-paired client home."""
    return {"TMUX_REMOTE_HOME": paired_client_home}


@pytest.fixture
def client_home(tmp_path):
    """A fresh temporary client home directory for tests that need one."""
    home = str(tmp_path / "client_home")
    os.makedirs(home, exist_ok=True)
    yield home
    shutil.rmtree(home, ignore_errors=True)
