# Claude auto-memory snapshot

Snapshot of Claude Code's auto-memory directory for this repo, checked
in for dev-box portability. Each file is a single memory record;
`MEMORY.md` is the index Claude loads at conversation start.

## What's here

Three memory types appear in the index:

- `feedback_*` — corrections and confirmations from past sessions. Each
  leads with the rule, then **Why** and **How to apply** lines.
- `project_*` — codices and ongoing-initiative context (Ngoma, Alembic,
  JF, multi-output framework, redesign docs, etc.).
- `reference_*` — pointers to external systems / branches / docs.

## Rehydrating on a new dev box

Claude reads auto-memory from
`~/.claude/projects/-home-sure-repos-er-301-habitat/memory/`. To
restore on a fresh machine:

```sh
mkdir -p ~/.claude/projects/-home-sure-repos-er-301-habitat/memory
cp docs/claude-memory/*.md \
   ~/.claude/projects/-home-sure-repos-er-301-habitat/memory/
```

The project-dir slug is derived from the repo's absolute path; if the
new box clones to a different path, the slug changes. Match the
clone path (`~/repos/er-301-habitat`) or rename the slug to fit.

## Keeping the snapshot fresh

This directory is a point-in-time copy, not a live mirror. Claude
writes to `~/.claude/...`, not here. Re-sync before any planned dev-box
swap:

```sh
cp ~/.claude/projects/-home-sure-repos-er-301-habitat/memory/*.md \
   docs/claude-memory/
git add docs/claude-memory && git commit -m "Refresh claude-memory snapshot"
```
