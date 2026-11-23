# MForce

MForce is a C++ platform for making sound and music. Sound is a graph of
generators, filters, modulators and envelopes in which any parameter can
itself be a signal. Music is an object model of rhythm, pitch, melody and
harmony, and composition is built algorithmically from those parts.

Today the engine is driven from the command line (`mforce_cli`); a node
editor is planned. It is built and tested on Windows only.

## What you need

- Visual Studio 2022 with the C++ desktop workload
- CMake 3.20 or later
- Python 3 with `lizard` (`pip install lizard`), for the structure gate

## Build

```
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --parallel
```

Every compiler warning is an error. A clean build takes about twelve
minutes on four cores.

Add `-DMFORCE_SHELVED=ON` to the first command to also build the shelved
composition strategies, their tests and `ppl_to_json`. Seven of the
templates in `scores/baselines/` need them.

## Test

```
python tools/gates.py --fast
python tools/gates.py
```

The first runs the test programs (one per engine module), the structure
gate and the tool tests: under a minute. The second adds three slower
gates: every library and baseline patch renders to the same bytes as its
recorded hash, no patch allocates memory while a note sounds, and every
baseline template composes to the same bytes. The last of these needs a
build with `-DMFORCE_SHELVED=ON`; without it the full run says so and
fails.

Neither command builds. Both read their list of tests from CMake, so
`ctest --test-dir build -C Release` runs the same set.

## Hear something

```
build\tools\mforce_cli\Release\mforce_cli.exe patches\library\winds\oboe1.json renders\oboe1.wav
```

The tool does not create folders. `renders/` is not tracked; it exists
once the tests have run, otherwise create it first.

A patch (`patches/`) is an instrument: a sound graph and how it is
played. Most carry a short score of their own, which is what this
renders. Among the tool's other modes, `--compose` composes a piece from
a template in `scores/` and `--check` reads and builds patches without
rendering them.

## Where things are

| Folder | What it holds |
|---|---|
| `engine/<module>/` | One folder per module: `include/`, `src/`, `tests/`. A module's `CMakeLists.txt` names the modules it may use, and the compiler refuses an include of any other. |
| `engine/testing/` | The test harness every module's tests use. |
| `tools/mforce_cli/` | The command-line renderer and composer. |
| `tools/structure/` | The structure gate: file and function size limits and the module map. |
| `patches/library/`, `patches/baselines/` | Finished instruments, and the patches the gates render. |
| `scores/` | Composition templates and scores. |
| `docs/architecture/` | The design of each part, and `MODULES.md`, the module map. |
