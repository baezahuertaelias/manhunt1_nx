# Project rules

Native Nintendo Switch port of **Manhunt (original Xbox XBE)** via hybrid static recompilation. Goal and milestones: [docs/ROADMAP.md](docs/ROADMAP.md). Process detail: [docs/WORKFLOW.md](docs/WORKFLOW.md).

## Evidence

- Correctness and executed evidence outrank speed and token savings.
- Never claim that a tool or API works, a test passed, a regression is fixed, or that FPS, compatibility, completeness or model availability hold without an executed test and accessible evidence. Mark anything unexecuted, including unavailable hardware, tools, assets and tests, `UNVERIFIED`. Never infer a pass.
- Emulator FPS is diagnostic only and never counts as Switch hardware FPS. Keep emulator-runner failures separate from port failures.
- Never claim Ghidra found every function. Track unknown and unresolved code regions explicitly.
- Unverified AI interpretations of code are hypotheses, not facts.
- Reviewers inspect the artifacts themselves, not only the author's summary.

## Legal and data

- Use lawfully obtained game and system files only. Never download, commit or redistribute proprietary game assets, BIOS, firmware, keys, SDK components or copyrighted binaries. User inputs stay under gitignored `assets/`.
- Never commit secrets, generated distributables, huge traces or caches.
- The GitHub repository is **public** (user decision 2026-10-28), so every commit is published: never commit game files or anything derived from them (lifted C, frames, logs, binaries). No releases or binary artifacts without explicit user authorization. Test artifacts must exclude proprietary data unless the user explicitly authorizes lawful storage.

## Host and Docker

- Project-scoped Docker work is allowed without asking: build, start, stop and rebuild containers, and install packages *inside* containers.
- Host package, driver, security or service changes require explicit approval. Never touch unrelated containers, images, networks, volumes or host data. Never mount an unrestricted Docker socket into untrusted services.
- Containers run non-root and least-privileged, get `/dev/dri` only where needed, have CPU/RAM limits, and use named volumes for persistent state. The host has 4 CPUs (Intel N150): serialize heavy Ghidra, compile and emulator jobs.
- Debug interfaces (GDB, emulator control) bind to localhost or go over SSH, never to public ports.
- Pin versions by digest or hash and record them. Verify download provenance and never run untrusted scripts blindly.
- Never inject host credentials into containers or print tokens.

## Change gates

- Every meaningful change carries its build and test results, the original guest addresses where applicable, and a short rationale.
- **Critical changes** (recompilation, guest memory, graphics, physics, AI, save system, emulator automation) go through **Implement → Build → Focused tests → Independent review by a different agent → Merge**. A failed review blocks the merge. Untested critical behaviour fails closed.
- Never push directly to `main`, force-push `main`, delete `main`, or bypass branch protection. Use no destructive Git operations without a project-scoped justification, and preserve user changes.
- Configuration is code. A config change must update `docs/CONFIGURATION.md` and `docs/CHANGELOG_CONFIG.md` in the same change, and gets independent review.
- No blanket stubs or no-op API replacements that hide crashes, unless the harmless behaviour is proven and documented.
- A gameplay difference is allowed only with a measured performance benefit, documented behavioural impact, reviewer approval and, where feasible, a reversible switch. Never break mission completion, stealth or executions, essential AI, combat or difficulty to gain FPS.

## Planning

- Analyse before creating tasks. Each task gets an ID, owner, scope, dependencies, acceptance criteria, expected artifacts and a rollback, recorded in `tasks/tasks.jsonl` and `TASK_GRAPH.md`.
- After 2–3 failed hypotheses, stop guessing: gather new evidence, switch technique, or escalate. When a major architectural assumption fails, stop and write a short options analysis with a timeboxed proof.
- Measure progress in playable behaviour, not in functions translated or tasks closed.

## Ask the user first

Host changes, legal or credential-sensitive operations, irreversible actions outside the project, and making anything public.

## Durable state

Keep task state and provenance in `tasks/`, `reports/`, `BETA_STATUS.md`, `BLOCKERS.md`, `BUILD_LOG.md` and `NEXT_ACTION.md` (the single highest-priority next action).
