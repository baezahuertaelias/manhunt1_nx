# Workflow

How work is planned, executed, reviewed and recorded. The hard rules are in [AGENTS.md](../AGENTS.md).

## Agent roles

Configure Codex CLI agents in `.codex/agents/*.toml`, but only after checking the installed Codex version's actual config syntax. Use the strongest *available* models at high reasoning effort for hard work; never hardcode model IDs that aren't available. Roles can be combined if concurrency is limited:

1. **Lead architect / integration owner:** milestones, interfaces, priorities, acceptance decisions.
2. **Reverse engineering:** XBE, Ghidra, call graphs, ABI, x86 semantics, Xbox API identification.
3. **Switch runtime:** AArch64, libnx, guest memory mapping, input, filesystem, audio, graphics, shaders.
4. **Emulator / test:** xemu reference, Eden/Ryujinx, input replay, differential evidence, diagnostics.
5. **Independent reviewer:** checks evidence, the diff, builds and traces for regressions, unsupported assumptions, missing evidence and scope creep. Never rubber-stamps.

Each agent works in its own Git branch or worktree, with explicit ownership of files and interfaces.

## Planning loop (per milestone or new blocker)

1. **Discover:** inspect the actual code, Ghidra evidence, traces, interfaces, tests and milestone status. State the unknowns.
2. **Plan:** critical path, dependency graph, integration boundaries, expected evidence, resource cost, risk. No exhaustive whole-game analysis before the first level boots.
3. **Decompose:** small, independently testable tasks with ID, owner, files, prerequisites, acceptance, artifacts and rollback. Avoid broad shared-file edits. Split large tasks.
4. **Schedule:** run independent tasks in parallel in isolated worktrees with defined API contracts. Never write shared generated artifacts, SQLite databases or config concurrently. Serialize CPU/GPU-heavy jobs on the 4-CPU host; each heavy job may use all cores (`CPUS`, default `nproc`; user decision 2026-10-26).
5. **Integrate:** build, run focused tests, get independent review of critical changes, merge through PRs, run post-merge end-to-end tests.
6. **Reassess:** update priorities and cancel obsolete tasks. Reanalysis is mandatory after repeated failed hypotheses.

The ledger is `tasks/tasks.jsonl`, mirrored in `TASK_GRAPH.md` with links to issues and PRs. Task count is not progress.

## Crash-to-function debug loop

1. Reproduce with an automated input script or checkpoint.
2. Capture the host crash, guest PC, call stack, registers, memory context and logs.
3. Resolve addresses against Ghidra and `FUNCTION_MAP.sqlite`.
4. Inspect the original assembly, callers and callees, memory and API semantics; compare with xemu where possible.
5. Make the **smallest evidence-backed fix** and add or update a focused regression test.
6. Rebuild, retest, compare checkpoints, and record the outcome.
7. After 2–3 failed hypotheses, gather new evidence, switch technique, or escalate.

## GitHub pipeline (public repo)

- Use the host's already-authenticated `gh`. Verify auth and repository identity before mutating anything. The repo is **public** by user decision (2026-10-28). Never publish proprietary inputs or derived game artifacts. Inspect any existing suitable repo before creating a new one.
- Agents may manage branches, issues, PRs, labels and milestones. Commits are descriptive and reference task IDs and evidence.
- Merge approved PRs into `main` automatically once required checks pass. Critical changes also need the independent AI review, with blocking findings addressed. Branch protection enforces the checks and blocks direct pushes. AI review does not replace a GitHub-native approval where one is required.
- **Pre-merge** tests must be sufficient evidence to merge. **Post-merge**, every merge to `main` triggers a fresh NRO build, Eden/Ryujinx regression tests (when a verified runner exists), host smoke tests and provenance checks. Major milestones also trigger xemu reference tests. Unavailable tests are marked `UNVERIFIED`, never green.
- On a post-merge failure: flag `main` as broken, open a high-priority issue, and land a corrective PR or a safe revert through normal review.
- GPU/emulator tests use a project-scoped local runner on this host. A self-hosted GitHub Actions runner is allowed only if it can be deployed without host changes. GitHub-hosted Actions may run safe build and lint checks that don't need a GPU or game files.

## Recovery

Orchestrator tasks are idempotent and checkpointable. Keep the following outside ephemeral containers:

- task ledger, worktree and branch IDs
- input hashes, last successful test
- logs, partial outputs, pending reviews

After an SSH drop, container restart, interrupted agent or reboot, inspect durable state and resume. Don't rerun destructive steps or open duplicate PRs. Use bounded retries and escalate repeated failures.

## Resources

Detect actual RAM, disk, CPU quota and GPU contention. Enforce Docker limits and concurrency caps. Record OOM and disk-full events and pause lower-priority work. Never trade data integrity for throughput.

## Provenance

Every NRO, log, trace, screenshot, benchmark, checkpoint, crash and test result carries a manifest linking:

- Git commit, task or PR ID
- container image digest, toolchain versions (devkitPro, libnx, deko3d, recompiler)
- config hash, XBE hash, asset manifest hash
- emulator name and version, hardware and firmware if any
- test status

Milestone-gate evidence is kept immutable, with retention and size policies, and filtered for secrets and proprietary data. Build entries go in `BUILD_LOG.md`.

## Reporting

At each milestone, report:

- what works, with evidence
- what is unverified
- what failed
- the next P0/P1 blocker
- performance data and its source
- the exact next action (also in `NEXT_ACTION.md`)

Report progress as playable behaviour, not as a percentage of functions translated.
