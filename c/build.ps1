$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
$out = Join-Path $root "build\mothpad_tests.exe"

New-Item -ItemType Directory -Force $build | Out-Null
$env:TMP = (Resolve-Path $build).Path
$env:TEMP = $env:TMP

$msysMingwBin = "C:\msys64\mingw64\bin"
if (Test-Path -LiteralPath $msysMingwBin) {
    $env:PATH = "$msysMingwBin;$env:PATH"
}

$candidates = @(
    "C:\msys64\mingw64\bin\gcc.exe",
    "C:\msys64\clang64\bin\clang.exe",
    (Join-Path $env:USERPROFILE "OneDrive\Desktop\emsdk\upstream\bin\clang.exe")
)

$cc = $null
foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
        $cc = $candidate
        break
    }
}

if (!$cc) {
    throw "No C compiler found. Checked: $($candidates -join ', ')"
}

Write-Host "Compiler: $cc"

& $cc `
    -std=c99 `
    -Wall `
    -Wextra `
    -Wpedantic `
    -I (Join-Path $root "src") `
    (Join-Path $root "src\mothpad.c") `
    (Join-Path $root "tests\mothpad_tests.c") `
    -o $out

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (!(Test-Path -LiteralPath $out)) {
    throw "Compiler reported success but did not create $out"
}

& $out
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
