# Build helper — every path is anchored to the folder holding this script, so the
# repository can live anywhere on disk without touching any other checkout.
$root = $PSScriptRoot
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2>$null
if (-not $vsPath) {
    $vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"
}
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
$proj = Join-Path $root "CrossPlatformGameEngine.sln"
$log  = Join-Path $root "buildlog.txt"

$script = "@echo off`r`ncall `"$vcvars`" x64 >nul 2>&1`r`nmsbuild `"$proj`" /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal > `"$log`" 2>&1`r`n"
$tmpBat = [System.IO.Path]::GetTempFileName() + ".bat"
[System.IO.File]::WriteAllText($tmpBat, $script)
$proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$tmpBat`"" -Wait -PassThru -NoNewWindow
Remove-Item $tmpBat -ErrorAction SilentlyContinue
Write-Output "Exit code: $($proc.ExitCode)"
