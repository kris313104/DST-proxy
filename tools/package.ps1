# Packages prox_voice.exe + runtime DLLs into dist/prox-voice and zips it for distribution.
# Usage:  pwsh tools/package.ps1 [-Config Release]
# NOTE: for a public release the .exe should be code-signed (Authenticode) to avoid
#       SmartScreen warnings - that requires a signing certificate (TODO, deployment step).
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$rel  = Join-Path $repo "app\build-msvc\$Config"
$dist = Join-Path $repo "dist\prox-voice"

if (-not (Test-Path (Join-Path $rel "prox_voice.exe"))) {
    throw "prox_voice.exe not found in $rel - build with -DPROX_WITH_VOICE=ON first."
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist -Force | Out-Null

Copy-Item (Join-Path $rel "prox_voice.exe") $dist
Get-ChildItem $rel -Filter *.dll | Copy-Item -Destination $dist
# CA bundle for wss:// TLS verification (ships next to the exe).
$ca = Join-Path $rel "cacert.pem"
if (Test-Path $ca) { Copy-Item $ca $dist }

@"
Proximity Voice for Don't Starve Together
=========================================
1. Run prox_voice.exe. A small window opens; it auto-installs the mod into your
   DST install. Enable it once in-game: DST -> Mods -> Client Mods -> "Proximity
   Voice Export" -> Enable, then restart DST.
2. Join a world with a friend. The app auto-joins that world's voice room - no codes.
   In the window you can: Mute/Unmute, pick your Microphone, and adjust Mic volume,
   Hearing range, and Audio direction (pan) live.
3. In-game mute: press N while playing to mute/unmute without alt-tabbing (a red
   "MIC MUTED" shows on screen). Change the key in DST -> Mods -> the mod's config.
4. Use HEADPHONES to avoid echo/feedback.

Advanced (command line): run with --console for text mode. Useful flags:
  --far <units> (audible range), --ptt <KEY> (push-to-talk), --mute <KEY>,
  --mic <index> / --list-devices, --mic-gain <x>, --pan <0..1>, --pan-offset <deg>,
  --name <YourName>, --signal <ws-url>.
"@ | Set-Content (Join-Path $dist "README.txt") -Encoding utf8

$zip = Join-Path $repo "dist\prox-voice.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$dist\*" -DestinationPath $zip

Write-Host "Packaged: $zip"
Get-ChildItem $dist | Select-Object Name, Length
