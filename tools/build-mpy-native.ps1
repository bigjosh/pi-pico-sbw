param(
    [string]$PythonExe = "C:/Users/passp/AppData/Local/Python/pythoncore-3.14-64/python.exe",
    [string]$BashExe = "D:/devkitPro/msys2/usr/bin/bash.exe",
    [string]$ArmBinDir = "C:/Users/passp/pico/bin"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$nativeDir = Join-Path $repoRoot "mpy/native"
$nativeDirPosix = "/" + (($nativeDir -replace "\\", "/") -replace ":", "")
$pythonPosix = "/" + (($PythonExe -replace "\\", "/") -replace ":", "")
$armBinPosix = "/" + (($ArmBinDir -replace "\\", "/") -replace ":", "")

& $BashExe -lc "export PATH=${armBinPosix}:`$PATH; make -C $nativeDirPosix PYTHON=$pythonPosix"
