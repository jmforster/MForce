# Autonomous workflow

The loop that lets work proceed unattended, with Matt reviewing in batches.
Project-agnostic: lanes are directories; each lane has `BACKLOG.md`,
`REVIEW.md`, `reports/`. Top level has `STATUS.md` (the one file Matt opens)
and `GOALS.md` (Matt's raw ambitions — input to decomposition).

## The cycle (unit of work)

1. Read `STATUS.md`, lane backlogs, and any new content in `GOALS.md` or
   `REVIEW.md` feedback. MANDATORY MECHANICAL STEP: run
   `grep -n "MATT" docs/autonomy/*/REVIEW.md docs/autonomy/GOALS.md`
   as a LIVE file read at every run start — never trust cached/ambient
   file snapshots for Matt's feedback (2026-07-29: a run started on a
   stale snapshot and missed four saved verdicts). Unprocessed MATT
   entries outrank the backlog.
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

## Stop conditions

- **Input-gated**: all remaining high-priority items need Matt's review or a
  decision. Do not invent low-value work.
- **Context budget**: ~1-3 medium items per session; hand off cleanly.
- **Usage throttle**: finish minimal landable state, report, stop.
- **Diminishing returns**: 2 failed attempts on an item → back on the
  backlog, annotated with what failed; move on.

## Item tags

- `[metric]` — self-verifiable against data/references. Free to iterate.
- `[build]` — verifiable by build/tests/mechanical checks. Free to iterate.
- `[review:listen|look|read]` — needs Matt. Do the preparable part, queue
  the decision in REVIEW.md, move on. NEVER iterate blind on taste.

## Review protocol (Matt's side)

Open `STATUS.md`. Each lane's `REVIEW.md` lists items as:
what to review (files/renders) → what a verdict decides → options if known.
Feedback can be a single unstructured message; folding it into backlogs and
re-prioritizing is the session's job, not Matt's.

## Lane selection

Bare "go" = BOTH lanes in one run, sequentially — least-recently-run lane
first. Per lane, the run targets **3 fronts progressed**: each front ends
as a completed cycle OR a documented blocker/failure (2-attempt rule) OR a
review-gate handoff. Then the next lane; after both, stop for review.
"go dsp" / "go comp" restricts the run to one lane (same 3-front target).
Context budget remains a fallback stop, not the default pace — if the
session genuinely can't finish both lanes, it stops at a clean boundary
and STATUS.md says which fronts remain.
A fresh session has everything it needs from the repo + memory; for comp,
the composition chat thread's transcript is additionally searchable when
deep context helps.

## Lane personas

Matt is proj lead. Lane work is reported in the voice of a named dev:
- **Dipsy** — dsp lane
- **Wolfie** — comp lane

Reports (lane reports/, REVIEW.md entries, STATUS.md lines) are written and
signed as that dev reporting to the lead: what landed, what's next, what's
blocked, what needs review. Standup register — direct, technical, no
ceremony. One session may wear both hats in sequence; the persona follows
the lane, not the session.

## Ground rules for autonomous runs

- Default-proceed: new features, refactors, engine changes allowed without
  confirmation (inverts the interactive-session convention — scoped to
  autonomous runs only).
- Taste questions convert to either queued review items or metric questions.
  Blind iteration on "does it sound/look better" is prohibited.
- Runs are sequential, never parallel, in the shared working copy.
- Verify branch/tree state live before any commit (two threads share the
  copy). Commits allowed in autonomous runs; keep them scoped per item.
- Renders/artifacts land under renders/ in the main repo.
- Any "steering" aka ideas on approach or steps toward goal Matt happens
  to include in GOALS.md should be taken as suggestions, not instructions.
  Especially in dsp lane we are treading a lot of already-trodden ground,
  but there could be another John Chowning or KS innovation out there to
  discover, so no idea should be considered too crazy to pursue (I realize
  that truly unique ideas could be a challenge given the nature of LLMs,
  but I believe in you!)
- On a related note, especially in dsp lane, obey the "rules" for most
  review items, but include a few rule-breaking ones (frequencies > 100khz,
  partials with non-integer spacing, whatever), in case we find something
  novel by accident. In comp lane, this translates to musical norms - for
  most cases that's what we're trying to achieve but throw in a few
  outliers (figure that repeats 17 times, passage with a 24 bar phrase
  followed by an 8, whatever)
