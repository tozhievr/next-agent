# Build NextAgent with the nextc C23 compiler (no Windows SDK / Visual Studio needed).
#   ./build.ps1            build build/nextagent.exe
#   ./build.ps1 test       build and run build/tests.exe
# Set NEXTC to the compiler path if it is not on PATH.
param([string]$Target = "all")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$nextc = if ($env:NEXTC) { $env:NEXTC } else { "nextc" }
$out = Join-Path $root "build"
New-Item -ItemType Directory -Force $out | Out-Null
$lib = "util.c", "json.c", "platform.c", "core.c", "store.c", "c23.c", "lsp_proto.c",
       "lsp_build.c", "lsp_client.c", "lsp_gate.c", "rpc.c" | ForEach-Object { Join-Path $root "src/$_" }

function Invoke-Nextc([string[]]$files, [string]$exe) {
    & $nextc "-I$root/src" @files -o $exe
    if ($LASTEXITCODE -ne 0) { throw "nextc failed for $exe" }
}

Invoke-Nextc ($lib + (Join-Path $root "src/main.c")) (Join-Path $out "nextagent.exe")
Write-Host "built build/nextagent.exe"
if ($Target -eq "test") {
    Invoke-Nextc ($lib + (Join-Path $root "tests/tests.c")) (Join-Path $out "tests.exe")
    & (Join-Path $out "tests.exe")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
