# Packages prox_voice.exe + runtime DLLs into dist/prox-voice and zips it for distribution.
# Usage:  pwsh tools/package.ps1 [-Config Release]
# NOTE: for a public release the .exe should be code-signed (Authenticode) to avoid
#       SmartScreen warnings — that requires a signing certificate (TODO, deployment step).
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$rel  = Join-Path $repo "app\build-msvc\$Config"
$dist = Join-Path $repo "dist\prox-voice"

if (-not (Test-Path (Join-Path $rel "prox_voice.exe"))) {
    throw "prox_voice.exe not found in $rel — build with -DPROX_WITH_VOICE=ON first."
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
1. Subscribe to the Steam Workshop mod "Proximity Voice Export".
2. Run prox_voice.exe — it auto-detects the DST log and joins your world's voice room:
     prox_voice.exe --signal ws://<server-host>:8080 --name YourName
3. Use HEADPHONES to avoid echo/feedback.

Useful flags: --far <units> (audible range), --ptt <KEY> (push-to-talk), --pan <0..1>.
"@ | Set-Content (Join-Path $dist "README.txt") -Encoding utf8

$zip = Join-Path $repo "dist\prox-voice.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$dist\*" -DestinationPath $zip

Write-Host "Packaged: $zip"
Get-ChildItem $dist | Select-Object Name, Length
