# Delegation Task Packet

Use one packet per tightly bounded worker change. Codex, as lead architect,
fills this out before invoking either implementation worker and reviews the
produced diff against the same packet.

```text
Task ID:
Assigned implementation worker:
Objective:
Why now:

Observed evidence:
- File/symbol:
- Current behavior:
- Lead source-inspection conclusion:

Owned files or symbols:
-

Excluded systems and non-goals:
-

Concurrent packet exclusions, if other implementers are active:
-

Design constraints:
- Existing repository convention:
- Lead-approved conclusion from reference research, if applicable:
- Data ownership/lifetime requirement:
- Coordinate or rendering convention, if applicable:

Acceptance criteria:
-

Permitted work:
- Read owned files/symbols and declarations immediately required by the edit.
- Edit only the owned boundary.

Forbidden work:
- Broad source discovery, external reference/online research, compilation, tests, broad formatting, dependencies, generated assets.
- Commits, pushes, destructive Git operations, unrelated cleanup.

Lead review checklist:
- Read every modified path and full diff.
- Confirm no concurrent worker changed this packet's owned files or symbols.
- Identify defects, design drift, lifetime issues, and boundary violations.
- Delegate a focused repair packet or reject the diff when improvements are required.
- State verification as not executed unless separately authorized later.
```

## Verification Packet

The lead creates this only after reviewing an implementation diff. The
verifier may execute commands, but it may not edit or publish.

```text
Task ID:
Reviewed diff or commit partition covered:
Required platform coverage:

Commands to execute exactly:
-

Expected evidence:
- Build success or actionable compiler/linker diagnostics.
- Test pass/fail output with failed test names and useful error excerpts.

Forbidden work:
- Editing, dependency changes, commits, pushes, cleanups, or substituted checks.
```

## Landing Packet

The lead creates this only after code review and adequate verification. One
packet may contain multiple commits only when each partition is explicitly
listed.

```text
Task ID:
Current branch:
Push destination: origin trunk
Verification evidence accepted by lead:
-

Commit partition:
- Subject:
  Body, if required:
  Owned paths or explicit hunks:

Publisher requirements:
- Inspect every status path before staging.
- Stop if status or diff no longer matches this packet.
- Stage only named paths/hunks; never use `git add .`.
- Inspect `git diff --cached --stat` and `git diff --cached` before each commit.
- Push only `origin trunk`, after the approved partition has been committed.
- Never amend, rewrite, or force push.
```
