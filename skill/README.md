# NextAgent skill for Claude Code

A skill that teaches Claude Code to edit and search code through the `nextagent` CLI: batch edits in one call and
one undoable revision, one function instead of a whole file, rename across the workspace, search by meaning. In a
modelled benchmark on QuickJS it needs about 2.6x fewer tokens and 29 -> 6 tool calls (docs/BENCHMARK.md in the
repository states how that was measured and what it does not show).

## Install

1. Install the `nextagent` binary (same release page): `install.sh` / `install.ps1`, or unpack the
   `nextagent-<version>-windows-x64.zip` / `-linux-x64.tar.gz` and put `nextagent` on PATH.
2. Unpack this package and run the installer:

```
./install-skill.sh              # Linux / macOS shell: ~/.claude/skills/nextagent
./install-skill.sh --project    # only for the current project: ./.claude/skills/nextagent
.\install-skill.ps1             # Windows PowerShell: %USERPROFILE%\.claude\skills\nextagent
.\install-skill.ps1 -Project
```

or copy the `nextagent/` folder to `~/.claude/skills/` yourself. Restart Claude Code (or start a new session) and the
skill `nextagent` is available; Claude loads it when a task matches its description, and you can name it explicitly.

## Contents

- `nextagent/SKILL.md` - when and how to use the tool (the part Claude reads first);
- `nextagent/reference.md` - every command, the error messages and the files in `.aed/`;
- `nextagent/VERSION` - the nextagent version this skill was written for.

Add `.aed/` to the `.gitignore` of every project you use it in. Source and issues:
https://github.com/tozhievr/next-agent
