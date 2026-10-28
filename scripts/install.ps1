# Copia TwitchRework.aex a la carpeta de plugins de After Effects.
# Pide permisos de administrador (Program Files está protegido).
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
Write-Host "Instalado en $dst"
Write-Host "Reinicia After Effects y busca: Efecto > Twitch Rework > Twitch Rework"
Start-Sleep -Seconds 3
