param(
    [string]$SrcDir
)

$SrcDir = $SrcDir.TrimEnd('\').TrimEnd('/')

$versionFile     = Join-Path $SrcDir "Version.id"
$buildInfoFile   = Join-Path $SrcDir "BuildInfo.h"
$releaseInfoFile = Join-Path $SrcDir "ReleaseInfo.md"

if (-not (Test-Path $versionFile)) {
    Write-Error "Version.id not found at: $versionFile"
    exit 1
}

$vc = [System.IO.File]::ReadAllText($versionFile)
if ($vc -notmatch "v(\d+)\.(\d+)\.(\d+)") {
    Write-Error "Could not parse version from Version.id"
    exit 1
}
$major = $Matches[1]
$minor = $Matches[2]
$build = $Matches[3]
$ver   = "v$major.$minor.$build"

# --- BuildInfo.h ---
$crlf = "`r`n"
$h  = "#pragma once$crlf"
$h += "$crlf"
$h += "// Authoritative build identity — update these on every release.$crlf"
$h += "// Format: v<BUILD_VERSION>.<BUILD_SUBVERSION>.<BUILD_NUMBER>$crlf"
$h += "constexpr int CURRENT_BUILD_VERSION    = $major;$crlf"
$h += "constexpr int CURRENT_BUILD_SUBVERSION = $minor;$crlf"
$h += "constexpr int CURRENT_BUILD            = $build;$crlf"
[System.IO.File]::WriteAllText($buildInfoFile, $h, [System.Text.Encoding]::UTF8)

# --- ReleaseInfo.md ---
if (Test-Path $releaseInfoFile) {
    $md = [System.IO.File]::ReadAllText($releaseInfoFile, [System.Text.Encoding]::UTF8)
    $md = $md -replace '\*Current Build Version: v\d+\.\d+\.\d+\*', "*Current Build Version: $ver*"
    [System.IO.File]::WriteAllText($releaseInfoFile, $md, [System.Text.Encoding]::UTF8)
}

Write-Host "  BuildInfo.h:    CURRENT_BUILD = $build"
Write-Host "  ReleaseInfo.md: Current Build Version: $ver"
Write-Host "  All version files synced to $ver"
