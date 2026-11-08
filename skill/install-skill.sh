#!/bin/sh
# Installs the NextAgent skill for Claude Code.
#   ./install-skill.sh              user-wide:    ~/.claude/skills/nextagent   (or $CLAUDE_SKILLS_DIR/nextagent)
#   ./install-skill.sh --project    this project: ./.claude/skills/nextagent
set -eu
here=$(cd "$(dirname "$0")" && pwd)
[ -f "$here/nextagent/SKILL.md" ] || { echo "install-skill.sh: run it from the unpacked skill package" >&2; exit 1; }
if [ "${1:-}" = "--project" ]; then dest="$PWD/.claude/skills"; else dest="${CLAUDE_SKILLS_DIR:-$HOME/.claude/skills}"; fi
mkdir -p "$dest"
rm -rf "$dest/nextagent"
cp -R "$here/nextagent" "$dest/nextagent"
echo "installed the nextagent skill $(cat "$dest/nextagent/VERSION") to $dest/nextagent"
if command -v nextagent >/dev/null 2>&1; then
  echo "found $(nextagent --version)"
else
  echo "note: the nextagent binary is not on PATH yet: curl -fsSL https://raw.githubusercontent.com/tozhievr/next-agent/main/install.sh | sh"
fi
