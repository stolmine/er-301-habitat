---
name: Persist plan-mode plans into the repo before executing
description: When a plan-mode workflow ends with ExitPlanMode, merge the plan-file content (or its substance) into the repo's planning/ directory before starting the implementation -- the ~/.claude/plans/<slug>.md file is ephemeral and will be lost across sessions.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
When plan mode produces a plan file at `~/.claude/plans/<slug>.md`, don't treat that as the final persistence. Before beginning execution, fold the plan's substance (affected files, sequencing, gotchas, risks, verification) into the appropriate `planning/*.md` file in the repo so it's versioned and survives the session.

**Why:** `~/.claude/plans/` is ephemeral. The repo's planning/ directory is the durable source of truth for future sessions (and future-me reading diffs). If a session gets interrupted mid-implementation, or a new session picks up the work, the plan needs to be in-repo.

**How to apply:**
- After ExitPlanMode is approved (or if the user redirects with "persist and go"), append or merge the relevant content into the project's planning doc BEFORE the first code edit.
- If there's already a planning doc for the topic, extend it -- don't create a parallel file. Keep one source of truth per topic.
- Commit the planning update as its own commit so the implementation PRs reference a stable plan artifact.
- The ephemeral plan file at `~/.claude/plans/` can be ignored after the persistence step; don't maintain both.

User's invocation pattern: "persist plan elements in md and then go for it" = do the persistence step then begin execution in the same turn.
