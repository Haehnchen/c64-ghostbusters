# Mandatory project basis

- The port was derived solely from `Ghostbusters_v2.crt`, now archived outside the product tree.
- SHA-256: `23d87f3a284b198bf4a5ed0e52fe0c1caadd3dbbb87a93931f17b19fd541cb6e`.
- Analysis, native porting and runtime comparisons must use exactly this version.
- Do not restart version selection or introduce additional game images without a concrete need and agreement with the user.
- `assets/fonts/game_font.json` contains the embedded 8x8 glyphs derived from the C64 system font, not another game version. Its source and original byte hash are retained in the asset metadata; no system ROM file is needed for builds or tests.
- Original extraction tools, disassembly and comparison evidence are archived
  outside this repository. Normal development uses the checked-in named assets;
  do not restore a reference or analysis dependency to the build.

# Collaboration and delegation

- Delegate independently actionable tasks; usually use up to two subagents in parallel.
- **Sol (`gpt-5.6-sol`), reasoning `medium`:** reverse engineering, control-flow/hardware analysis, complex implementation and reviews.
- **Luna (`gpt-5.6-luna`), reasoning `xhigh`:** small bounded implementations, data extraction, tests and mechanical changes.
- Match agents to task type; do not attach a major architectural decision to a small support task.
- Explicitly set model and reasoning at startup; provide targeted context with `fork_turns="none"` and a complete task description.
- Every delegation names its goal, exclusively owned files, acceptance checks and the sole CRT reference.
- All agents share the directory. Do not revert others' changes; coordinate shared files.
- The main agent owns workflow, build and integration, and reviews results before committing.
- The goal is a functional standalone native port, not a cycle-accurate C64 recreation. Historical timing reports are analysis evidence, not additional completion conditions.
- Prepare and embed assets, including music, effects and speech, once. Product builds and runtime must not require the CRT, original payload or reference directory. Named data replaces original-address access in product code. Provenance and original mappings remain in asset metadata and archived analysis/export material.
- Prioritize complete scenes and game flows. Every scene includes its evidenced music, effects and speech; graphics/logic-only tests do not prove complete porting. Investigate IRQ/CIA/NMI details only for a concrete gameplay, audible or visible defect.
- Record verified changes and relevant checks in commit messages. Commit verified packages separately and regularly.
- Standard commands: `make doctor`, `make build`, `make run`, `make check`.
- Unit tests mirror `src/` directories and module names under `tests/`.
  Connected game tests and input schedules belong under `tests/integration/`;
  Python tool tests belong under `tests/tools/`. Keep long paths opt-in.
- Project prose, Markdown, code comments and test messages must be English. Preserve identifiers, source quotes and game data; do not revert translated prose to German.
- Public README scope (clarified 2026-09-20): keep only a simple getting-started guide with the minimum prerequisites and build/start commands needed to run the game. No architecture, implementation details, asset formats, analysis reports, or technical background. Lasting technical explanations belong in code comments, not the README.
- Keep lasting technical knowledge directly in the relevant code as concise English comments: formats, invariants, edge cases, and reasons for non-obvious decisions. Do not create a documentation tree, replacement Markdown files, or asset READMEs. Original-source details belong in archived analysis/export code, not product runtime code. Keep legally required notices intact.

# Mandatory behavior acceptance

- Acceptance covers standalone data/build, complete game rules, connected transitions, natural games, and picture/sound/pacing. Include shopping, city, driving, capture/failure, HQ, bait, Marshmallow, Zuul, both endings, account reuse, and restart.
- Fully map and verify original paths, including sections between previous capture boundaries; passing isolated routine tests does not prove a complete scene.
- Every missing or unverified path needs a concrete task, reproducible check and acceptance criterion; do not leave it as a permanent limitation.
- Natural complete games without injected capture/haunting states, together with moving pictures, audio, input and timing, are required for acceptance.
- Timing means suitable game pace and correct event order, not identical milliseconds or hardware cycles. Align comparisons to game/scene events and check every gameplay-relevant path; pure hardware mechanisms may have native equivalents.
- Use `make check TEST=pattern` for affected short checks; `make check-extended` runs four connected routes and `make check-all` adds the vehicle/equipment/outcome variants. `make check-standalone` verifies a fresh source build and executable-only startup. Dummy audio and scene exports do not establish practical listening or desktop-control acceptance.
- Repeat checks for relevant behavior/build changes or concrete defects, not comment-only changes. Existing practical acceptance does not require a new campaign or hardware-timing reconstruction.
