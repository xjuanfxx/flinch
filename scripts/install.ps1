# Copies Flinch.aex into the After Effects plug-ins folder.
# Asks for administrator rights (Program Files is protected).
param(
    [string]$AeVersion = "2026",
    [string]$Config = "Release"
)

$src = Join-Path $PSScriptRoot "..\build\$Config\Flinch.aex" | Resolve-Path -ErrorAction Stop
$dst = "C:\Program Files\Adobe\Adobe After Effects $AeVersion\Support Files\Plug-ins\Flinch"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).
    IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process pwsh -Verb RunAs -Wait -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"",
        "-AeVersion", $AeVersion, "-Config", $Config)
    exit
}

# Remove the install from before the rename (the effect was called "Twitch Rework").
$legacy = Join-Path (Split-Path $dst) "TwitchRework"
if (Test-Path $legacy) {
    Remove-Item -Recurse -Force $legacy
    Write-Host "Removed old install: $legacy"
}

New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item $src $dst -Force
Write-Host "Installed to $dst"
Write-Host "Restart After Effects and look for: Effect > Flinch > Flinch"
Start-Sleep -Seconds 3
