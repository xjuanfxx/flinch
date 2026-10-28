#!/bin/sh
# Installs TwitchRework.plugin into After Effects (macOS).
# Usage: sh install-mac.sh /path/to/TwitchRework.plugin [AE version, default 2026]
set -e

plugin="${1:?Pass the path to TwitchRework.plugin}"
version="${2:-2026}"
dest="/Applications/Adobe After Effects $version/Plug-ins/TwitchRework"

# Remove the "downloaded from the internet" flag so macOS does not block it.
xattr -dr com.apple.quarantine "$plugin" 2>/dev/null || true

sudo mkdir -p "$dest"
sudo rm -rf "$dest/TwitchRework.plugin"
sudo cp -R "$plugin" "$dest/"
echo "Installed to $dest"
echo "Restart After Effects and look for: Effect > Twitch Rework > Twitch Rework"
