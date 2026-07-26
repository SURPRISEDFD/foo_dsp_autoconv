# Downloads and unpacks the official foobar2000 SDK into <repo root>\SDK.
#
# The official download is currently a .7z archive (older releases were .zip);
# both are handled. The filename changes with every SDK release - check
#   https://www.foobar2000.org/SDK
# and pass -Url (or edit the default / the FB2K_SDK_URL env in build.yml)
# when a new SDK is published.
#
# .7z extraction requires 7-Zip (7z.exe). GitHub Actions windows runners have
# it preinstalled; locally install it from https://www.7-zip.org/ if missing.
#
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File tools\get_sdk.ps1
#   powershell -ExecutionPolicy Bypass -File tools\get_sdk.ps1 -Url https://www.foobar2000.org/downloads/SDK-XXXX-XX-XX.7z

param(
    [string]$Url  = "https://www.foobar2000.org/downloads/SDK-2025-03-07.7z",
    [string]$Dest = "SDK"
)

$ErrorActionPreference = "Stop"

# Keep the URL's real extension: it selects the extractor, and Expand-Archive
# insists on .zip anyway.
$ext = [IO.Path]::GetExtension(([Uri]$Url).AbsolutePath).ToLowerInvariant()
if ([string]::IsNullOrEmpty($ext)) { $ext = ".7z" }
$archive = Join-Path ([IO.Path]::GetTempPath()) ("fb2k_sdk" + $ext)

Write-Host "Downloading foobar2000 SDK from $Url"
$ok = $false
for ($i = 1; $i -le 3 -and -not $ok; $i++) {
    try {
        Invoke-WebRequest -Uri $Url -OutFile $archive -UseBasicParsing
        $ok = $true
    } catch {
        Write-Warning "Attempt ${i} failed: $_"
        if ($i -lt 3) { Start-Sleep -Seconds (5 * $i) }
    }
}
if (-not $ok) {
    throw "Failed to download the SDK. Check the URL against https://www.foobar2000.org/SDK and update FB2K_SDK_URL / -Url."
}

if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }

function Find-7Zip {
    foreach ($name in @("7z", "7z.exe")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    $candidates = @()
    if ($env:ProgramFiles)        { $candidates += (Join-Path $env:ProgramFiles        "7-Zip\7z.exe") }
    if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe") }
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

switch ($ext) {
    ".7z" {
        $sevenZip = Find-7Zip
        if (-not $sevenZip) {
            throw "The SDK archive is a .7z file but 7-Zip (7z.exe) was not found. Install it from https://www.7-zip.org/ (GitHub Actions windows runners already include it)."
        }
        Write-Host "Extracting with 7-Zip: $sevenZip"
        & $sevenZip x $archive "-o$Dest" -y | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7-Zip extraction failed (exit code $LASTEXITCODE)." }
    }
    ".zip" {
        Expand-Archive -Path $archive -DestinationPath $Dest -Force
    }
    default {
        throw "Unsupported archive type '$ext' - expected .7z or .zip."
    }
}

# Some SDK archives wrap everything in a single top-level folder - flatten if so.
if (-not (Test-Path (Join-Path $Dest "pfc/pfc.vcxproj"))) {
    $sub = Get-ChildItem $Dest -Directory | Select-Object -First 1
    if ($sub -and (Test-Path (Join-Path $sub.FullName "pfc/pfc.vcxproj"))) {
        Get-ChildItem $sub.FullName -Force | Move-Item -Destination $Dest
        Remove-Item $sub.FullName -Recurse -Force
    }
}

if (-not (Test-Path (Join-Path $Dest "pfc/pfc.vcxproj"))) {
    throw "SDK layout not recognized (pfc/pfc.vcxproj missing). The SDK structure may have changed - inspect the archive contents."
}

# Normalize the shared import library names across SDK versions:
# older SDKs ship shared.lib (x86); newer ones ship shared-Win32.lib.
$sharedDir = Join-Path $Dest "foobar2000/shared"
if ((Test-Path (Join-Path $sharedDir "shared.lib")) -and
    -not (Test-Path (Join-Path $sharedDir "shared-Win32.lib"))) {
    Copy-Item (Join-Path $sharedDir "shared.lib") (Join-Path $sharedDir "shared-Win32.lib")
    Write-Host "Normalized shared.lib -> shared-Win32.lib"
}

# Align the SDK projects' PlatformToolset with ours (v143 / VS2022).
# The SDK ships v142 project files and builds with /GL (LTCG); /GL object
# files are NOT binary-compatible across compiler versions, so mixing the
# SDK's v142 libs with our v143 build fails at link time with error C1047.
Get-ChildItem $Dest -Recurse -Filter *.vcxproj | ForEach-Object {
    $content = Get-Content $_.FullName -Raw
    $patched = $content -replace '<PlatformToolset>v14[012]</PlatformToolset>', '<PlatformToolset>v143</PlatformToolset>'
    if ($patched -ne $content) {
        [IO.File]::WriteAllText($_.FullName, $patched)
        Write-Host "Patched PlatformToolset -> v143: $($_.FullName)"
    }
}

Write-Host "SDK ready at $Dest"
