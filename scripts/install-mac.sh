#!/bin/sh
# Instala TwitchRework.plugin en After Effects (macOS).
# Uso: sh install-mac.sh /ruta/a/TwitchRework.plugin [versión de AE, por defecto 2026]
set -e

plugin="${1:?Indica la ruta a TwitchRework.plugin}"
version="${2:-2026}"
dest="/Applications/Adobe After Effects $version/Plug-ins/TwitchRework"

# Quita la marca de "descargado de internet" para que macOS no lo bloquee.
xattr -dr com.apple.quarantine "$plugin" 2>/dev/null || true

sudo mkdir -p "$dest"
sudo rm -rf "$dest/TwitchRework.plugin"
sudo cp -R "$plugin" "$dest/"
echo "Instalado en $dest"
echo "Reinicia After Effects y busca: Efecto > Twitch Rework > Twitch Rework"
