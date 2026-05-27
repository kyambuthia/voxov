# Delegated Engine Development Workflow

This workflow uses OpenCode workers as constrained contributors while Codex,
acting as the lead architect, retains scope, design, code review, improvement
decisions, and landing decisions. Worker models do not approve each other's
code. Codex does not personally compile code in this workflow; approved
verification is delegated to a dedicated worker. A change is never described
as complete or verified merely because a worker wrote it.

The lead alone reads and reasons from repository source, external
game-development texts, and online technical sources. The lead may use Codex
subagents as supervised source-inspection assistants, but OpenCode workers
receive conclusions as task-packet constraints and are not delegated discovery
or architecture research.

## Roles

| Role | OpenCode agent | Model | Authority |
| --- | --- | --- | --- |
| Lead architect | Codex supervising session | Codex | Inspects source and references, defines boundaries, evaluates diffs, accepts or rejects work, controls commits |
| Implementation worker | `voxov-implementer` | GLM 5.1 | Edits only approved files/symbols; no compilation, tests, commits, or scope growth |
| Implementation worker | `voxov-deepseek-worker` | DeepSeek V4 Pro | Edits only approved files/symbols; useful for analysis-heavy tasks; no compilation, tests, commits, or scope growth |
| Implementation worker | `voxov-kimi-worker` | Kimi K2.6 | Edits only approved files/symbols; no compilation, tests, commits, or scope growth |
| Implementation worker | `voxov-qwen-worker` | Qwen 3.7 Max | Edits only approved files/symbols; no compilation, tests, commits, or scope growth |
| Implementation worker | `voxov-minimax-worker` | MiniMax M2.7 | Edits only approved files/symbols; no compilation, tests, commits, or scope growth |
| Implementation worker | `voxov-mimo-worker` | MiMo V2.5 Pro | Edits only approved files/symbols; no compilation, tests, commits, or scope growth |
| Verification worker | `voxov-verifier` | DeepSeek V4 Pro | Runs lead-approved CMake/CTest checks in canonical build directories; reports errors; no edits or Git publishing |
| Publisher | `voxov-publisher` | GLM 5.1 | Stages an approved commit partition, commits, and pushes only after lead review and verification evidence |

The configured implementation models are the OpenCode Go identifiers
`opencode-go/glm-5.1`, `opencode-go/deepseek-v4-pro`,
`opencode-go/kimi-k2.6`, `opencode-go/qwen3.7-max`,
`opencode-go/minimax-m2.7`, and `opencode-go/mimo-v2.5-pro`.

## Control Loop

1. The lead inspects `git status --short`, repository instructions, relevant
   code, and the current roadmap before delegating.
2. The lead reads any relevant extracted texts or online sources, evaluates
   them against the code and roadmap, and turns accepted conclusions into
   concrete design constraints.
3. The lead creates a task packet using
   [delegation_task_packet.md](delegation_task_packet.md), recording approved
   constraints without delegating the underlying research.
4. The lead revises and approves a single implementation boundary. Planet
   coordinate math, terrain/meshing, streaming/LOD, renderer resources,
   movement/camera, collision, atmosphere, tests, and documentation remain
   separate boundaries unless the lead records why they cannot be separated.
5. The lead chooses one implementation worker for each approved packet. More
   than one editing worker may run only when each has its own packet, the
   packets own disjoint files and symbols, and each resulting diff is
   independently reviewable and landable.
6. The assigned worker receives only its approved packet, reads only local
   code necessary to execute it, and writes the smallest coherent diff. It
   reports recommended checks but does not run them.
7. The lead reads every changed path and the complete diff, checking behavior,
   ownership, lifetime, coordinate conventions, GPU/resource behavior, error
   handling, unplanned changes, and relevant reference guidance.
8. The lead produces concrete review findings and improvement instructions,
   then either rejects the work or delegates a bounded repair packet back to a
   worker. This review is not delegated.
9. When the diff meets the packet, the lead issues a verification packet to
   `voxov-verifier` naming the exact commands and required platform coverage.
10. The verifier compiles/runs tests only as directed and returns diagnostics.
    Failures return to the lead for diagnosis and a focused implementation
    repair packet; the verifier does not fix code.
11. After acceptable code review and sufficient verification evidence, the
    lead issues a landing packet defining each commit subject, owned
    paths/hunks, verification record, branch, and push destination.
12. The publisher follows `AGENTS.md`: inventories status, refuses mixed
    commits, stages intentionally, reviews the staged diff, commits, and
    pushes only the approved partition to `origin trunk`. It does not decide
    what to ship.

## Invocation

Run a worker from the repository root with a task packet included in the
prompt:

```bash
opencode run --agent voxov-implementer "Implement this approved task packet only: <packet>"
opencode run --agent voxov-deepseek-worker "Implement this approved task packet only: <packet>"
opencode run --agent voxov-kimi-worker "Implement this approved task packet only: <packet>"
opencode run --agent voxov-qwen-worker "Implement this approved task packet only: <packet>"
opencode run --agent voxov-minimax-worker "Implement this approved task packet only: <packet>"
opencode run --agent voxov-mimo-worker "Implement this approved task packet only: <packet>"
opencode run --agent voxov-verifier "Run this approved verification packet only: <packet>"
opencode run --agent voxov-publisher "Execute this approved landing packet only: <packet>"
```

Implementation workers cannot compile, test, commit, or push. The verifier
can compile and test but cannot edit or publish. The publisher can stage,
commit, and push a lead-approved verified partition to `origin trunk` but
cannot edit code, compile, or force push. Tool permissions deny browsing,
unapproved shell operations, subagent delegation, skills, and unapproved
external paths; the verifier has narrow exceptions for the configured Web and
Android SDK paths.

Task packets and Codex review enforce file/symbol ownership and the prohibition
on broad source inspection. OpenCode permissions do not prove semantic edit
boundaries, so a worker diff is never accepted solely because tool permissions
allowed it.

## Parallel Implementation Rules

Additional implementers increase throughput only for separable work. Codex
must not assign two editing workers overlapping files or symbols in the same
working tree, and must not use parallel edits to bypass the planet rewrite
boundaries or commit partition rules in `AGENTS.md`.

Each simultaneously active implementation packet records its assigned worker,
owned files or symbols, excluded neighboring systems, and expected landing
partition. If one worker discovers a required cross-boundary edit, it stops
and returns the dependency for Codex to re-scope; it does not negotiate edits
with another worker.

## Review Standard

A worker diff is not accepted until the lead can answer:

- Does it implement only the requested behavior and preserve unrelated work?
- Is ownership placed in the existing subsystem rather than expanding
  `Engine` or duplicating runtime logic?
- For world and planet changes, do rendering and collision read consistent
  source data and coordinate conventions?
- For rendering changes, are resource creation, update, eviction, and draw
  lifetimes explicit and compatible with the supported Sokol/GLES3/WebGL2
  direction?
- Are performance claims backed by a measurable counter or an explicitly
  deferred measurement plan?
- Did the verifier report exact executed checks and their results?
- Does each publisher landing packet contain a review-approved commit split
  and verification evidence adequate for its claims?

Suggestions from a worker become work only after the lead evaluates and
scopes them into a new task packet.
