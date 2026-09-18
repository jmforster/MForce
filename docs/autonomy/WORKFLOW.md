# Autonomous workflow

The loop that lets work proceed unattended, with Matt reviewing in batches.
Rewritten 2026-09-14 around the 2026-09-13 steering meeting: ONE dev (the
personas Dipsy/Wolfie are retired), lanes survive as backlog namespaces.
Each lane directory has `BACKLOG.md`, `REVIEW.md`, `reports/`. Top level
has `STATUS.md` (the SESSION HANDOFF file — Claude opens it first; Matt
doesn't read it, confirmed 2026-09-14), `GOALS.md` (Matt's raw ambitions —
input to decomposition), and `IDEAS.md` (parked, undecided).

**Matt's interface is the two REVIEW files.** He goes straight to
dsp/REVIEW.md and comp/REVIEW.md to see what's waiting for him. HARD
RULE: anything that needs Matt's ears, eyes, or a decision MUST have a
REVIEW entry — a queue or question that exists only in STATUS or a
backlog section is a routing bug (2026-09-14: three weeks of September
ears queues lived only in STATUS and were invisible to him).

## The cycle (unit of work)

1. Read `STATUS.md`, lane backlogs, and any new content in `GOALS.md` or
   `REVIEW.md` feedback. MANDATORY MECHANICAL STEP: run
   `grep -n "MATT" docs/autonomy/*/REVIEW.md docs/autonomy/GOALS.md`
   as a LIVE file read at every run start — never trust cached/ambient
   snapshots for Matt's feedback (2026-07-29: a run started on a stale
   snapshot and missed four saved verdicts). Unprocessed MATT entries
   outrank the backlog.
2. If `GOALS.md` has undecomposed goals: decompose into backlog items first.
3. Pick the top item NOT gated on review. Spec briefly (inline for small,
   docs/superpowers/specs/ for engine-level), implement, verify, render any
   listenables/viewables.
4. Write a dated report to the lane's `reports/`, update `BACKLOG.md`,
   queue review items in `REVIEW.md`, refresh `STATUS.md`.
5. Repeat until a stop condition.

Invariant: never stop mid-cycle. Every stopping point is a verified,
reported, resumable state in repo files. Items too big for one cycle get
split into staged sub-items, each independently landable.

## Campaigns

Big multi-round efforts (the STK port, harness rounds, a search family) are
single large backlog items called campaigns. A campaign item carries its
own STOP CONDITIONS (steering decision 2026-09-13) — rounds-without-a-keeper
limits, measurement ceilings, "parked until X" gates — written in the item,
so a run knows when to stop without asking.

## Steal-first (dsp model, steering 2026-09-13)

For any known instrument/mechanism: vendor proven code first, render its
WAVs as ground truth, port the mechanism into the graph, THEN extend from
the proven point. Do not re-derive published models from first principles.
Rate discipline from the STK port: validate a port AT the donor's native
rate; canonicalize to 48k as a separate, measured step (every fixed
constant sorted physical-Hz vs normalized-frequency). Scorers (ML-ears,
comp metrics) get cheap discrimination backtests before their numbers are
trusted for verdicts.

Start from the state of the art (Matt, 2026-09-12; repo-visible here since
2026-09-17): this is well-trodden ground — before DESIGNING any round on a
known mechanism, fetch the published standard model (JOS/waveguide/MSW,
the campaign's donor digests) and express THAT, then refine. Never build a
round on our nearest existing approximation when the published component is
already recovered and expressible. The one sanctioned exception is a
mechanism-isolation probe (validating a single new element on a known-good
carrier) — and then the state-of-the-art pairing is the NEXT round, by
default, not an afterthought. (Rule made explicit after the 09-17 nonlinear
bore day: the steepener probe was a legitimate isolation round, but its
follow-up got dispatched on the known-inferior memoryless lip while the
published one-mass lip sat recovered in the digest.)

## Run contract — machine verdicts and the ears budget

- Machine gates may self-verdict REJECTS: a cell that fails its stated
  gate (pitch lock, click, silence, envelope) is logged, its patch moves
  to patches/old/, its renders are DELETED (derived data). No Matt pass
  needed for failures.
- **Gen scripts OWN their output dirs**: before staging, purge any file
  not in the current cell set. A cell rename between script versions
  left stale runaway WAVs interleaved with quiet cells in an audition
  queue (2026-09-16 — Matt's speakers). Corollary: every queue gets a
  LEVEL-SAFETY gate — no 0.5 s window above rms 0.5 (sustained level is
  the speaker hazard, not momentary peak) and an audibility floor (the
  silent-parity lesson, REVIEW 59). Queue renders also get LOUDNESS
  CALIBRATION: peak-normalize to −6 dBFS (or match a library reference)
  unless the cell's character forbids it — Matt at 100% volume barely
  hearing a queue (REVIEW 62, 09-17) is an audition blocker, and
  twin/control pairs must stay matched after normalization.
- **REVIEW entries in PLAIN LANGUAGE** (Matt, 09-17): no campaign
  jargon in headers or bodies ("the passive round" → say what it is);
  supersede an Awaiting item the moment its replacement queues; fold
  chat verdicts into REVIEW the same turn they arrive — a verdict that
  lives only in conversation is invisible to Matt's own queue and he
  will rightly ask "didn't I respond to this?".
- Near-duplicate culling by perceptual distance is allowed before queueing.
- What survives to Matt is capped: **ears budget ≤ 20 items per run,
  honest count** (an A/B pair counts as what it costs to audition).
- Keep-verdicts flow the other way: the future audition dashboard's Keep
  button writes to pending/<campaign>/ as MATT'S click (his live-play
  bench). Runs themselves still NEVER write pending/. Library promotion
  from the bench stays Matt-only, per artifact.

## Volume expectation (Matt, 2026-07-29)

Scheduled runs must deliver VOLUME: hours of work, many deliverables,
multiple fronts — not one item. Backlog items are largely independent:
tackle them, don't re-file them. Adding an item to the backlog instead of
doing it needs a reason (review-gated, genuinely blocked, or >1 session
of work — then stage it and DO the first stage). When context limits a
single session, fan out well-specified independent fronts to subagents
(disjoint files, at most one C++ builder at a time) rather than deferring.

## Stop conditions

- **Campaign-local**: whatever the campaign item declares.
- **Input-gated**: all remaining high-priority items need Matt's review or
  a decision. Do not invent low-value work.
- **Context budget**: ~1-3 medium items per session; hand off cleanly.
- **Usage throttle**: finish minimal landable state, report, stop.
- **Diminishing returns**: 2 failed attempts on an item → back on the
  backlog, annotated with what failed; move on.

## Item tags

- `[metric]` — self-verifiable against data/references. Free to iterate.
- `[build]` — verifiable by build/tests/mechanical checks. Free to iterate.
- `[review:listen|look|read]` — needs Matt. Do the preparable part, queue
  the decision in REVIEW.md, move on. NEVER iterate blind on taste.

## Review-file hygiene

When a MATT comment is folded (acted on), REMOVE the raw comment from
"Awaiting" in the same run: move a compact summary + the verbatim text
(if it has reference value) to "Resolved" with the fold date. "Awaiting"
must contain ONLY items still needing Matt's input.

## Review protocol (Matt's side)

Open `dsp/REVIEW.md` and `comp/REVIEW.md` — nothing else required. Each
lists items as:
what to review (files/renders) → what a verdict decides → options if known.
Feedback can be a single unstructured message; folding it into backlogs and
re-prioritizing is the session's job, not Matt's. The audition dashboard
(queue-aware play/A-B/verdict UI writing structured verdict files) is a
future brainstorm — until it exists, verdicts arrive as messages/REVIEW
notes.

## Lane selection

One dev runs both lanes; the lane is a namespace, not a hat. Bare "go" =
BOTH lanes in one run, sequentially — least-recently-run lane first. Per
lane, target **3 fronts progressed**: each front ends as a completed cycle
OR a documented blocker/failure (2-attempt rule) OR a review-gate handoff.
Then the next lane; after both, stop for review. "go dsp" / "go comp"
restricts to one lane (same target). Context budget remains a fallback
stop, not the default pace. Reports are plain first-person — standup
register, direct, technical, no ceremony, no persona signatures.

## Misc task lists (MISC.md)

Matt may drop a dated list of small, concrete tasks in a lane's `MISC.md`.
Do-as-written items — no decomposition pass — and they outrank the backlog
when Matt points a run at them. Completed items flow through the normal
machinery; a finished list is replaced by a pointer to its report.

## Ideas file (IDEAS.md)

`docs/autonomy/IDEAS.md` holds plausible-but-undecided features so they do
not clutter the backlogs. Runs read it for context and NEVER act on an
entry; promotion to a lane backlog is Matt's call, and a promoted entry
leaves the file the same day. Backlog housekeeping flows the same
direction: maybe-someday backlog items move HERE, with a ledger note
keeping the old id resolvable (fall-cleaning rule, 2026-09-14). Matt may
also dictate late-night ideas into IDEAS.md dated (accepted in principle
09-13) — same read-never-act rule.

## Ground rules for autonomous runs

- Default-proceed: new features, refactors, engine changes allowed without
  confirmation (inverts the interactive-session convention — scoped to
  autonomous runs only).
- Taste questions convert to either queued review items or metric
  questions. Blind iteration on "does it sound/look better" is prohibited.
- NEVER promote to library/ (patches or renders) without Matt's audition
  and approval of that exact artifact. New machine-passed work lands in
  patches/audition/ with a [listen] item; patches/pending/ and
  renders/*/pending/ are Matt's sandboxes — runs never write there.
  Earlier verdicts on precursors do not transfer to a new render.
- Runs are sequential, never parallel, in the shared working copy.
- Verify branch/tree state live before any commit (sessions share the
  copy). Commits allowed in autonomous runs; keep them scoped per item.
  While subagents are active in the tree, `git add -u` is FORBIDDEN —
  stage explicit paths only.
- Renders/artifacts land under renders/ in the main repo.
- Any engine (engine/) edit rebuilds BOTH mforce_cli AND mforce_ui in the
  same cycle; `mforce_ui.exe --stamp` must exit 0 before the cycle closes.
- A running mforce_ui.exe is NOT a blocker for engine work: rename the
  running exe, link the new build into place, `--stamp` exit 0. If rename
  fails, complete ALL the code anyway, ask Matt to close the UI, relink
  after. Never drop engine fronts because the UI is up.
- Regression scope = the null gate (tools/null_gate_perform_source.py):
  patches/library/ + patches/baselines/, minus the library/voice vowel
  skip (one sung + one spoken representative; Matt 2026-09-14). Baselines
  were set-cover culled to ~40 on 2026-09-14 — one representative per
  node/loader feature plus cited repro patches; culled files live in
  patches/old/baselines_culled_2026-09-14/. Run the gate once per commit
  batch (not per tweak); refreeze only when truth deliberately changes,
  in the same commit as the change.
- Any "steering" aka ideas on approach or steps toward goal Matt happens
  to include in GOALS.md should be taken as suggestions, not instructions.
  Especially in dsp lane we are treading a lot of already-trodden ground,
  but there could be another John Chowning or KS innovation out there to
  discover, so no idea should be considered too crazy to pursue.
- Obey the "rules" for most review items, but include a few rule-breaking
  ones (frequencies > 100 kHz, non-integer partial spacing), in case we
  find something novel by accident. In comp, this translates to musical
  norms — mostly aim for them, throw in a few outliers (a figure that
  repeats 17 times, a 24-bar phrase followed by an 8).
- Subagents run long jobs (renders, builds, batch scoring) in the
  FOREGROUND — no run_in_background, no monitors. Completion wakeups do
  not reliably reach subagents (2026-08-04: three agents hung for hours;
  one background build died silently corrupting the incremental build).
  Backgrounding is for the top-level session only. Subagents do NOT
  commit — the coordinator commits after verification.
