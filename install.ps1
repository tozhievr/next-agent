# NextAgent installer: downloads the latest release, verifies its SHA-256 and
# adds it to the user PATH.
#   irm https://raw.githubusercontent.com/tozhievr/next-agent/main/install.ps1 | iex
# Optional environment variables (set before running):
#   NEXTAGENT_VERSION  tag to install, e.g. v0.20.0 (default: latest)
#   NEXTAGENT_DIR      install directory (default: %LOCALAPPDATA%\NextAgent\bin)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repo = "tozhievr/next-agent"
$dir = if ($env:NEXTAGENT_DIR) { $env:NEXTAGENT_DIR } else { Join-Path $env:LOCALAPPDATA "NextAgent\bin" }
$api = if ($env:NEXTAGENT_VERSION) { "https://api.github.com/repos/$repo/releases/tags/$($env:NEXTAGENT_VERSION)" }
       else { "https://api.github.com/repos/$repo/releases/latest" }

$rel = Invoke-RestMethod -Uri $api -Headers @{ "User-Agent" = "nextagent-installer" }
$zip = $rel.assets | Where-Object { $_.name -like "nextagent-*-windows-x64.zip" } | Select-Object -First 1
$sum = $rel.assets | Where-Object { $_.name -eq "SHA256SUMS.txt" } | Select-Object -First 1
if (-not $zip) { throw "release $($rel.tag_name) has no windows-x64 zip" }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("nextagent-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $zipPath = Join-Path $tmp $zip.name
    Invoke-WebRequest -Uri $zip.browser_download_url -OutFile $zipPath
    if ($sum) {
        $sumPath = Join-Path $tmp "SHA256SUMS.txt"
        Invoke-WebRequest -Uri $sum.browser_download_url -OutFile $sumPath
        $line = Get-Content $sumPath | Where-Object { $_ -match [regex]::Escape($zip.name) } | Select-Object -First 1
        if (-not $line) { throw "no checksum for $($zip.name)" }
        $want = ($line -split "\s+")[0].ToLower()
        $got = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower()
        if ($want -ne $got) { throw "checksum mismatch: expected $want, got $got" }
    }
    New-Item -ItemType Directory -Force $dir | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $tmp -Force
    $exe = Get-ChildItem $tmp -Recurse -Filter nextagent.exe | Select-Object -First 1
    if (-not $exe) { throw "nextagent.exe not found in archive" }
    Copy-Item $exe.FullName (Join-Path $dir "nextagent.exe") -Force
} finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $dir) {
    [Environment]::SetEnvironmentVariable("Path", (($userPath, $dir) -join ";").Trim(";"), "User")
    Write-Host "added $dir to the user PATH (restart the terminal)"
}
& (Join-Path $dir "nextagent.exe") --version
Write-Host "installed $($rel.tag_name) -> $dir"
