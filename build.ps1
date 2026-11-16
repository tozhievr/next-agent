# C++26 / CMake frontend. Usage: ./build.ps1 [build|test|clean]
param([ValidateSet("build", "test", "clean")][string]$Target = "build")
$ErrorActionPreference = "Stop"
& python (Join-Path $PSScriptRoot "build.py") $Target
exit $LASTEXITCODE
