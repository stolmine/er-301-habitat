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

Claude reads auto-memory from `~/.claude/projects/<slug>/memory/`,
where `<slug>` is the repo's absolute path with `/` replaced by `-`
(e.g. `/home/bram/repos/er-301-habitat` -> `-home-bram-repos-er-301-habitat`).
Run from the repo root; the snippet derives the slug from `pwd`,
excludes this README, and copies every memory record plus `MEMORY.md`:

```sh
SLUG="$(pwd | tr / -)"
DEST="$HOME/.claude/projects/$SLUG/memory"
mkdir -p "$DEST"
find docs/claude-memory -maxdepth 1 -name '*.md' ! -name README.md \
  -exec cp {} "$DEST/" \;
```

## Keeping the snapshot fresh

This directory is a point-in-time copy, not a live mirror. Claude
writes to `~/.claude/...`, not here. Re-sync before any planned dev-box
swap (same slug derivation as above):

```sh
SLUG="$(pwd | tr / -)"
SRC="$HOME/.claude/projects/$SLUG/memory"
find "$SRC" -maxdepth 1 -name '*.md' -exec cp {} docs/claude-memory/ \;
git add docs/claude-memory && git commit -m "Refresh claude-memory snapshot"
```
