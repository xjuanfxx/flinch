# Copies TwitchRework.aex into the After Effects plug-ins folder.
# Asks for administrator rights (Program Files is protected).
param(
    [string]$AeVersion = "2026",
    [string]$Config = "Release"
)

$src = Join-Path $PSScriptRoot "..\build\$Config\TwitchRework.aex" | Resolve-Path -ErrorAction Stop
$dst = "C:\Program Files\Adobe\Adobe After Effects $AeVersion\Support Files\Plug-ins\TwitchRework"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).
    IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process pwsh -Verb RunAs -Wait -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"",
        "-AeVersion", $AeVersion, "-Config", $Config)
    exit
}

New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item $src $dst -Force
Write-Host "Installed to $dst"
Write-Host "Restart After Effects and look for: Effect > Twitch Rework > Twitch Rework"
Start-Sleep -Seconds 3
