$env:PICO_SDK_PATH = "C:\Users\passp\pico\pico-sdk-2.2.0"
$env:PICO_TOOLCHAIN_PATH = "C:\Users\passp\pico\bin"
$env:PIOASM_DIR = "C:\Users\passp\pico\pico-sdk-tools-2.2.0\pioasm"
$env:PICOTOOL_DIR = "C:\Users\passp\pico\picotool-2.2.0-a4\picotool"

$toolPaths = @(
    "C:\Users\passp\pico\bin",
    "C:\Users\passp\pico\pico-sdk-tools-2.2.0\pioasm",
    "C:\Users\passp\pico\picotool-2.2.0-a4\picotool"
)

$currentPath = ($env:PATH -split ';') | Where-Object { $_ }
for ($i = $toolPaths.Length - 1; $i -ge 0; $i--) {
    $toolPath = $toolPaths[$i]
    if ($currentPath -notcontains $toolPath) {
        $currentPath = @($toolPath) + $currentPath
    }
}

$env:PATH = ($currentPath -join ';')

Write-Host "PICO_SDK_PATH=$env:PICO_SDK_PATH"
Write-Host "PICO_TOOLCHAIN_PATH=$env:PICO_TOOLCHAIN_PATH"
Write-Host "PIOASM_DIR=$env:PIOASM_DIR"
Write-Host "PICOTOOL_DIR=$env:PICOTOOL_DIR"
