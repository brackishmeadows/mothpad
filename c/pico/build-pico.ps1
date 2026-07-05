param(
    [string]$Board = "pico2_w",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (!$BuildDir) {
    $BuildDir = Join-Path $root "build-$Board"
}

if (!$env:PICO_SDK_PATH) {
    throw "PICO_SDK_PATH is not set. Install pico-sdk and set PICO_SDK_PATH first."
}

if (!(Test-Path -LiteralPath $env:PICO_SDK_PATH)) {
    throw "PICO_SDK_PATH does not exist: $env:PICO_SDK_PATH"
}

$msysMingwBin = "C:\msys64\mingw64\bin"
if (Test-Path -LiteralPath $msysMingwBin) {
    $env:PATH = "$msysMingwBin;$env:PATH"
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (!$cmake) {
    throw "cmake is not on PATH. Install CMake or run from a Pico SDK developer shell."
}

$armGcc = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
if (!$armGcc) {
    Write-Warning "arm-none-eabi-gcc is not on PATH. CMake may still find it if the Pico SDK toolchain is configured, but a normal shell will probably fail."
}

cmake -S $root -B $BuildDir "-DPICO_BOARD=$Board"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
