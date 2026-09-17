# AGENTS.md

## Purpose

This file defines the default working rules for coding agents operating in this repository.

Project-specific architecture, commands, constraints, and milestones should live in the repository documentation rather than being duplicated here unless they are essential to safe development.

## Scope and Change Discipline

- Keep changes focused on the user's request and the current project milestone.
- Preserve existing behavior unless the task explicitly requires changing it.
- Do not modify unrelated files or discard existing user changes.
- Prefer simple, readable solutions over unnecessary abstractions.
- Follow the existing architecture and conventions before introducing new patterns.
- Reuse existing utilities and infrastructure where practical rather than duplicating functionality.
- Do not perform speculative refactors unless they are required for the requested change.

## Repository Awareness

Before making non-trivial changes:

1. Inspect the repository structure.
2. Inspect the working tree and preserve any pre-existing changes.
3. Read the relevant project documentation.
4. Review recent Git history when it helps explain current architecture or incomplete work.
5. Identify existing tests, build commands, coding conventions, and nearby implementations before editing.

Do not assume the repository is clean, complete, or in the same state as a previous session.

## Language and Code Style

- Use the language version configured by the repository.
- Follow the repository's existing formatting, naming, and layout conventions.
- Prefer clear, descriptive names.
- Keep functions and classes focused.
- Add comments where they explain intent, constraints, non-obvious behavior, protocol details, or workarounds.
- Avoid comments that merely restate the code.
- Preserve platform-independent/platform-specific separation where the project already uses one.
- Avoid introducing global state unless the existing architecture requires it.
- Prefer explicit ownership and lifetime management.

## Dependencies

- Keep dependencies minimal.
- Prefer existing project dependencies or the language/platform standard library when sufficient.
- Do not add, remove, or upgrade dependencies unless the requested change requires it.
- Declare dependencies using the repository's normal build/package system.
- Explain why a new runtime dependency is needed.
- Avoid adding large frameworks for narrow problems.

## Platform and Native-Code Safety

For projects involving native APIs, drivers, hooks, plugins, injected modules, game mods, or hardware interaction:

- Preserve calling conventions, ABI compatibility, structure layout, alignment, and bitness requirements.
- Keep 32-bit and 64-bit behavior consistent where both are supported.
- Treat external pointers, offsets, handles, callbacks, and device data as untrusted until validated.
- Do not make assumptions about undocumented memory layouts without diagnostics or supporting evidence.
- Avoid blocking sleeps on application-critical threads.
- Avoid detached background threads with unclear ownership or shutdown semantics.
- Keep initialization, shutdown, reconnect, pause/resume, and failure paths synchronized.
- Ensure resources are released cleanly on normal shutdown and partial initialization failure.
- Prefer bounded, reversible runtime hooks over broad process modifications.
- Do not weaken operating-system security features to make development builds work.

## Reverse Engineering and Compatibility Work

When working with undocumented software behavior:

- Separate observed facts from assumptions.
- Add diagnostics before building significant logic on top of uncertain data.
- Validate candidate fields or signals across multiple states before treating them as authoritative.
- Keep reverse-engineered offsets, signatures, structures, and interpretations documented.
- Avoid naming unknown fields too confidently until their behavior is established.
- Prefer resilient signatures/patterns over fixed addresses when the existing project architecture supports them.
- Fail safely when expected patterns, modules, devices, or structures are not found.
- Do not silently continue with guessed offsets or incompatible versions.

For force-feedback, input, physics, telemetry, audio, rendering, or similar real-time systems, validate each signal independently before combining them into a larger model.

## Configuration

- Follow the repository's existing configuration system.
- Preserve backwards compatibility for existing configuration values unless the task explicitly requires a breaking change.
- Provide safe defaults for missing or invalid values.
- Keep independent concepts in independent settings rather than overloading one option.
- Validate user-configurable ranges before applying them.
- Update example/default configuration files when adding or changing settings.

## Logging and Diagnostics

- Use the project's existing logging infrastructure.
- Log enough information to diagnose initialization failures and important state transitions.
- Keep verbose per-frame/per-event diagnostics opt-in.
- Avoid flooding normal logs with high-frequency data.
- Diagnostic output should expose raw source values where useful, not only derived results.
- Do not log secrets, credentials, authentication tokens, or unrelated personal data.

## Tests and Validation

- Add or update tests when behavior changes or a bug is fixed.
- Run relevant tests after code changes.
- Prefer focused tests during development and the repository's full validation suite before handoff.
- Exercise important error paths as well as the happy path.
- For hardware-, game-, GUI-, or environment-dependent behavior that cannot be automated, provide a clear manual validation procedure.
- Distinguish automated validation from physical/manual validation.

Before handing off completed work:

1. Build the affected targets.
2. Run relevant automated tests.
3. Run any repository-defined lint/static-analysis checks relevant to the change.
4. Verify command-line/help/configuration output when user-facing behavior changes.
5. Report any validation command that could not be run and explain why.

Use the exact project commands documented by this repository rather than inventing generic replacements.

## Git Safety

- Inspect the working tree before editing.
- Preserve pre-existing user changes.
- Do not stage or commit files unless the user explicitly asks.
- Do not amend commits, rebase, merge, push branches, force-push, open pull requests, or modify remote state unless explicitly requested.
- Avoid destructive commands such as `git reset --hard`, `git clean -fd`, or checkout/restore operations that discard user work.
- Do not rewrite repository history without explicit approval.
- When comparing against another branch, fork, or upstream repository, avoid changing the current branch merely to inspect it.

## Generated and Local Files

- Do not commit generated build outputs, IDE state, logs, caches, temporary files, local credentials, or machine-specific configuration unless the repository intentionally tracks them.
- Update `.gitignore` when new tools create recurring local artifacts.
- Do not delete unknown local files merely because they are untracked.

## Documentation

- Keep README files, project metadata, configuration examples, build instructions, and usage examples accurate when behavior changes.
- Document important architecture, protocol, reverse-engineering, or compatibility findings.
- Keep documentation concise enough that future sessions can use it effectively.
- Prefer documenting durable knowledge over narrating implementation history.

## Project Continuity

If the repository contains project planning/status documents such as:

- `docs/PROJECT_PLAN.md`
- `docs/PROJECT_STATUS.md`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT.md`

read the relevant documents before starting substantial work.

When `PROJECT_STATUS.md` or an equivalent session-handoff file exists:

- continue from the current milestone rather than restarting completed work;
- keep changes scoped to the active milestone;
- record significant technical findings and blockers;
- update status before finishing a substantial coding session;
- record manual tests the user still needs to perform;
- leave clear next actions for the following session.

Do not create planning/status documents solely because this file mentions them. Use them only when they fit the repository.

## Handoff Requirements

At the end of a coding task, summarize:

- what changed;
- important implementation decisions;
- files added or modified;
- tests/builds performed and their results;
- anything not validated;
- any manual testing still required;
- known limitations or follow-up work.

Be explicit about uncertainty. Do not report a feature as validated merely because it compiled.

## Project-Specific Instructions

Repository-specific rules should be added below this section.

Examples include:

- required compiler/language versions;
- exact build and test commands;
- directory/layout conventions;
- supported platforms and architectures;
- runtime installation paths;
- ABI/protocol compatibility constraints;
- game/version-specific offsets or signatures;
- hardware safety limits;
- current project milestones;
- required manual validation procedures.

Keep project-specific instructions narrow and factual so the generic rules above remain reusable across repositories.
