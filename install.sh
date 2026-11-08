#!/bin/sh
# NextAgent installer for Linux x86-64: downloads the latest release, verifies its SHA-256, installs the binary.
#   curl -fsSL https://raw.githubusercontent.com/tozhievr/next-agent/main/install.sh | sh
# Optional environment variables:
#   NEXTAGENT_VERSION  tag to install, e.g. v0.25.0 (default: latest)
#   NEXTAGENT_DIR      install directory (default: $HOME/.local/bin)
set -eu
repo="tozhievr/next-agent"
dir="${NEXTAGENT_DIR:-$HOME/.local/bin}"
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || { echo "install.sh: only Linux x86-64 is supported" >&2; exit 1; }
if [ -n "${NEXTAGENT_VERSION:-}" ]; then api="https://api.github.com/repos/$repo/releases/tags/$NEXTAGENT_VERSION"
else api="https://api.github.com/repos/$repo/releases/latest"; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
url=$(curl -fsSL "$api" | grep -o 'https://[^"]*nextagent-[^"]*-linux-x64\.tar\.gz' | head -1)
[ -n "$url" ] || { echo "install.sh: no linux-x64 asset in this release" >&2; exit 1; }
curl -fsSL "$url" -o "$tmp/pkg.tar.gz"
if curl -fsSL "$url.sha256" -o "$tmp/sum" 2>/dev/null; then
  want=$(cut -d' ' -f1 "$tmp/sum"); got=$(sha256sum "$tmp/pkg.tar.gz" | cut -d' ' -f1)
  [ "$want" = "$got" ] || { echo "install.sh: checksum mismatch" >&2; exit 1; }
fi
tar -xzf "$tmp/pkg.tar.gz" -C "$tmp"
mkdir -p "$dir"
install -m 755 "$tmp"/nextagent-*/nextagent "$dir/nextagent"
echo "installed $("$dir/nextagent" --version) to $dir"
case ":$PATH:" in *":$dir:"*) ;; *) echo "add $dir to your PATH" ;; esac
