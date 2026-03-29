# MForce project instructions

## Repositories
- Current C++ implementation: this repo
- Legacy C# implementation: ./mforce-legacy

When porting code:
- Always inspect the corresponding C# class first
- Then inspect the current C++ equivalent (if any)
- Then implement minimal changes

## Mission
Port the legacy C# DSP engine to modern C++20 on Windows first.
Preserve sound behavior unless explicitly told otherwise.

## Scope right now
- Core engine only
- No Unity/UI
- Build system is CMake
- CLI renderer is the primary harness
- Patch files drive testing
- Windows-only is fine for now

## Non-negotiables
- Do not restart from scratch
- Use the existing C++ repo as the scaffold
- Preserve the current CMake/build/render pipeline
- Preserve currently working RedNoise behavior unless fixing a confirmed parity bug
- Prefer minimal diffs
- No heap allocation in hot render loops
- Avoid reflection-like designs; prefer explicit registries
- Use deterministic seeds in examples and tests
- Any parameter of a ValueSource may itself be a ValueSource

## Architecture direction
- ValueSource graph model is fundamental
- Patch format should support:
  - literal constants
  - node references for modulation
- Shared modulators should use shared ownership where appropriate
- Keep the code real-time safe where practical

## Build and run
- Build from repo root
- Main executable: mforce_cli
- Write renders into renders/

## Validation expectations
After making code changes:
1. Build successfully
2. Run at least one relevant patch
3. Report exactly what changed
4. If behavior differs, explain whether it is intentional or a bug

## Near-term priorities
1. Add graph-based ValueSource wiring
2. Support constants or node refs for parameters
3. Add SineSource as first modulation source
4. Expand patch loading incrementally
5. Add small deterministic regression tests