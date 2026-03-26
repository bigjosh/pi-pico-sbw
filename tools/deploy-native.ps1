param(
    [string]$Port = "COM11",
    [string]$LocalPath = "mpy/native/sbw_native.mpy",
    [string]$RemotePath = "sbw_native.mpy",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$localFullPath = (Resolve-Path (Join-Path $repoRoot $LocalPath)).Path
$buildScript = Join-Path $repoRoot "tools/build-mpy-native.ps1"

if (-not $SkipBuild) {
    & $buildScript
}

$localHash = (Get-FileHash $localFullPath -Algorithm SHA256).Hash.ToLowerInvariant()

$remoteFsPath = $RemotePath
if ($remoteFsPath.StartsWith(":")) {
    $remoteFsPath = $remoteFsPath.Substring(1)
}
if (-not $remoteFsPath.StartsWith("/")) {
    $remoteFsPath = $remoteFsPath.TrimStart("/")
}

$remoteCpTarget = ":" + $remoteFsPath
$remotePyPath = $remoteFsPath.Replace("\", "\\").Replace("'", "\\'")

Write-Output "LOCAL_PATH=$localFullPath"
Write-Output "REMOTE_PATH=$remoteFsPath"
Write-Output "LOCAL_SHA256=$localHash"

& mpremote connect $Port cp $localFullPath $remoteCpTarget

$remoteHash = (& mpremote connect $Port exec "import hashlib,binascii;print(binascii.hexlify(hashlib.sha256(open('$remotePyPath','rb').read()).digest()).decode())").Trim().ToLowerInvariant()

Write-Output "REMOTE_SHA256=$remoteHash"

if ($remoteHash -ne $localHash) {
    throw "SHA256 mismatch after copy. Local=$localHash Remote=$remoteHash"
}

Write-Output "VERIFY=OK"
