# Homebrew Tap Design

## Problem

Users need a simple way to install tmux-remote on macOS and Linux without manually downloading release archives. Homebrew is the standard package manager for macOS and is also available on Linux.

## Approach

Create a Homebrew tap repository (`gammelby/homebrew-tap`) with a binary formula that downloads pre-built release archives. Automate formula updates via a new job in the existing release workflow.

## Tap Repository

Repository: `gammelby/homebrew-tap`

```
homebrew-tap/
  Formula/
    tmux-remote.rb
  README.md
```

Users install with:

```bash
brew tap gammelby/tap
brew install tmux-remote
```

## Formula Design

The formula is a binary distribution formula (not source-built) because the Nabto Client SDK shared library is bundled in the release archives.

Platform support:
- macOS arm64
- Linux amd64
- Linux arm64

The formula:
- Uses `on_macos`/`on_linux` and `Hardware::CPU.arm?` to select the correct archive
- Declares `depends_on "tmux"` (runtime dependency)
- Installs `tmux-remote` and `tmux-remote-agent` to `bin/`
- Installs `libnabto_client.dylib` or `libnabto_client.so` to `lib/`
- Fixes rpaths so binaries find the shared library in the Homebrew prefix

## Automation

A new `update-tap` job is added to `.github/workflows/release.yaml` after the `release` job:

1. Downloads the release archives
2. Computes SHA256 checksums
3. Generates the formula from a template with the correct version, URLs, and SHAs
4. Pushes the updated formula to `gammelby/homebrew-tap`

Authentication: A fine-grained PAT stored as `TAP_GITHUB_TOKEN` secret in `gammelby/tmux-remote`, scoped to `gammelby/homebrew-tap` with Contents:write.

## Setup Steps

1. Create `gammelby/homebrew-tap` repository on GitHub
2. Create a fine-grained PAT scoped to that repo (Contents:write)
3. Add the PAT as `TAP_GITHUB_TOKEN` secret in `gammelby/tmux-remote`
4. Add the `update-tap` job to `release.yaml`
5. The formula template is generated inline by the workflow (no template file needed)
