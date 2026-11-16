# NextAgent skill

Requires NextAgent 0.45.1. The binary exposes its bundled instructions directly:

```text
nextagent skill
nextagent skill --reference
nextagent skill --json
```

These commands work without a workspace or separate skill installation. They never send model requests. To register the skill with an agent, copy the `nextagent/` folder into that agent's skill directory (Codex: `~/.codex/skills/`; Claude Code: `~/.claude/skills/`). The existing `install-skill.ps1` and `install-skill.sh` helpers target Claude Code.

`SKILL.md` explains targeted reads, AST labels and checked transactions; `reference.md` provides API examples, limits and recovery. The canonical documents are embedded at build time and included in runtime archives. Keep workspace `.aed/` journals and caches out of git.
