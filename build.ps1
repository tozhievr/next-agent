# Build NextAgent (Windows x86-64).
#   ./build.ps1                          nextc (default): build/nextagent.exe
#   ./build.ps1 test                     build and run build/tests.exe
#   ./build.ps1 -Compiler clang          use clang (needs a Windows SDK/MSVC runtime)
#   ./build.ps1 test -Compiler clang
# nextc needs neither the Windows SDK nor Visual Studio; set NEXTC to its path if
# it is not on PATH. For clang, set CLANG if it is not on PATH.
param(
    [string]$Target = "all",
    [ValidateSet("nextc", "clang")][string]$Compiler = $(if ($env:NA_COMPILER) { $env:NA_COMPILER } else { "nextc" })
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = Join-Path $root "build"
New-Item -ItemType Directory -Force $out | Out-Null
$lib = "util.c", "json.c", "platform.c", "core.c", "store.c", "c23.c", "lsp_proto.c",
       "lsp_build.c", "lsp_client.c", "lsp_gate.c", "lsp_hub.c", "refactor.c", "index.c", "semantic.c", "hybrid.c", "rpc.c" | ForEach-Object { Join-Path $root "src/$_" }

function Invoke-Cc([string[]]$files, [string]$exe) {
    if ($Compiler -eq "clang") {
        $cc = if ($env:CLANG) { $env:CLANG } else { "clang" }
        & $cc -std=c2x -O2 -Wall "-I$root/src" @files -o $exe -lkernel32
    } else {
        $cc = if ($env:NEXTC) { $env:NEXTC } else { "nextc" }
        & $cc "-I$root/src" @files -o $exe
    }
    if ($LASTEXITCODE -ne 0) { throw "$Compiler failed for $exe" }
}

Invoke-Cc ($lib + (Join-Path $root "src/main.c")) (Join-Path $out "nextagent.exe")
Write-Host "built build/nextagent.exe ($Compiler)"
if ($Target -eq "test") {
    Invoke-Cc ($lib + (Join-Path $root "tests/tests.c")) (Join-Path $out "tests.exe")
    & (Join-Path $out "tests.exe")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
