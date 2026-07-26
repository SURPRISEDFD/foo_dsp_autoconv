# Fetches r8brain-free-src (MIT, (c) Aleksey Vaneev / Voxengo) into
# third_party/r8brain. It is used to resample calibration impulse responses
# to the stream sample rate when no exact-rate file exists.
#
#   https://github.com/avaneev/r8brain-free-src
#
# The repository publishes no release archives, so the default URL tracks the
# master branch. To pin an exact revision, pass:
#   -Url https://github.com/avaneev/r8brain-free-src/archive/<commit>.zip

param(
    [string]$Url  = "https://github.com/avaneev/r8brain-free-src/archive/refs/heads/master.zip",
    [string]$Dest = "third_party/r8brain"
)

$ErrorActionPreference = "Stop"

$zip = Join-Path ([IO.Path]::GetTempPath()) "r8brain.zip"
Write-Host "Downloading r8brain-free-src from $Url"
$ok = $false
for ($i = 1; $i -le 3 -and -not $ok; $i++) {
    try {
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
        $ok = $true
    } catch {
        Write-Warning "Attempt ${i} failed: $_"
        if ($i -lt 3) { Start-Sleep -Seconds (5 * $i) }
    }
}
if (-not $ok) { throw "Failed to download r8brain-free-src. Check the URL." }

$tmp = Join-Path ([IO.Path]::GetTempPath()) "r8brain_extract"
if (Test-Path $tmp)  { Remove-Item $tmp  -Recurse -Force }
if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $tmp -Force

# GitHub archives wrap everything in one top-level folder - flatten it.
$root = $tmp
if (-not (Test-Path (Join-Path $root "CDSPResampler.h"))) {
    $sub = Get-ChildItem $root -Directory | Select-Object -First 1
    if ($sub -and (Test-Path (Join-Path $sub.FullName "CDSPResampler.h"))) {
        $root = $sub.FullName
    }
}
if (-not (Test-Path (Join-Path $root "CDSPResampler.h"))) {
    throw "r8brain archive layout not recognized (CDSPResampler.h missing)."
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item (Join-Path $root "*") $Dest -Recurse -Force
Remove-Item $tmp -Recurse -Force
Remove-Item $zip -Force

Write-Host "r8brain-free-src ready at $Dest"
