# Installs the NextAgent skill for Claude Code.
#   .\install-skill.ps1              user-wide:    %USERPROFILE%\.claude\skills\nextagent   (or $env:CLAUDE_SKILLS_DIR)
#   .\install-skill.ps1 -Project     this project: .\.claude\skills\nextagent
param([switch]$Project)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path (Join-Path $here 'nextagent\SKILL.md'))) { throw 'install-skill.ps1: run it from the unpacked skill package' }
if ($Project) { $dest = Join-Path (Get-Location) '.claude\skills' }
elseif ($env:CLAUDE_SKILLS_DIR) { $dest = $env:CLAUDE_SKILLS_DIR }
else { $dest = Join-Path $env:USERPROFILE '.claude\skills' }
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$target = Join-Path $dest 'nextagent'
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse (Join-Path $here 'nextagent') $target
$ver = (Get-Content (Join-Path $target 'VERSION') -Raw).Trim()
Write-Host "installed the nextagent skill $ver to $target"
if (Get-Command nextagent -ErrorAction SilentlyContinue) { Write-Host ("found " + (& nextagent --version)) }
else { Write-Host 'note: the nextagent binary is not on PATH yet: irm https://raw.githubusercontent.com/tozhievr/next-agent/main/install.ps1 | iex' }
