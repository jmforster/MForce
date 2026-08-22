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

## Volume expectation (Matt, 2026-07-29)

Scheduled runs must deliver VOLUME: hours of work, many deliverables,
multiple fronts — not one item. Backlog items are largely independent:
tackle them, don't re-file them. Adding an item to the backlog instead of
doing it needs a reason (review-gated, genuinely blocked, or >1 session
of work — then stage it and DO the first stage). When context limits a
single session, fan out well-specified independent fronts to subagents
(disjoint files, at most one C++ builder at a time) rather than deferring.

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

## Review-file hygiene

When a MATT comment is folded (acted on), REMOVE the raw comment from
"Awaiting" in the same run: move a compact summary + the verbatim text
(if it has reference value) to "Resolved" with the fold date. "Awaiting"
must contain ONLY items still needing Matt's input — a stale processed
comment sitting in the queue cost a confused review pass on 2026-07-30.

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

## Misc task lists (MISC.md)

Matt may drop a dated list of small, concrete tasks in a lane's `MISC.md`
(pattern established with dsp/MISC.md, 2026-08-21). These are do-as-written
items — no decomposition or design pass needed — and they outrank the
backlog when Matt points a run at them. Completed items flow through the
normal machinery (report, gates, BACKLOG/REVIEW/STATUS updates); when a
list is done, replace it with a pointer to the report so the file only
ever holds outstanding tasks.

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
- NEVER promote to library/ (patches or renders) without Matt's audition
  and approval of that exact artifact. New work lands in pending/ with a
  [listen] item; "locked baseline" is Matt's to declare, and earlier
  verdicts on precursors do not transfer to a new render (2026-08-14: the
  08-13 evening run locked ks_piano_plausible unauditioned; demoted).
- Runs are sequential, never parallel, in the shared working copy.
- Verify branch/tree state live before any commit (two threads share the
  copy). Commits allowed in autonomous runs; keep them scoped per item.
  While subagents are active in the tree, `git add -u` is FORBIDDEN —
  stage explicit paths only (2026-07-29: a blanket -u swept an agent's
  half-finished file into an unrelated commit).
- Renders/artifacts land under renders/ in the main repo.
- Any engine (engine/) edit rebuilds BOTH mforce_cli AND mforce_ui in the
  same cycle — a cli-only rebuild leaves Matt's UI on a different engine
  (2026-08-01: stale-banner fired because a revert rebuilt cli only).
  `mforce_ui.exe --stamp` must exit 0 before the cycle closes.
- A running mforce_ui.exe is NOT a blocker for engine work (Matt,
  2026-08-20). The lock is on the final link of one exe only. Rename the
  running exe (mforce_ui_locked_<date>.exe), link the new build into
  place, `--stamp` exit 0 — Matt's open windows keep running the renamed
  image and pick up the new build on restart. If rename genuinely fails,
  complete ALL the code anyway, ask Matt to close the UI, relink after.
  Never drop engine fronts because the UI is up.
- Regression scope is patches/library/ (plus baselines/ when relevant),
  NOT the full tree (Matt, 2026-08-20). ~95% of patches outside library/
  are Claude-generated variants/sweeps/scratch; full-corpus byte-identical
  sweeps (196 patches, 1200 renders) are not required and must not hold
  up work. library/ is the Matt-approved set and the regression set.
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

- Subagents run long jobs (renders, builds, batch scoring) in the
  FOREGROUND — no run_in_background, no monitors, no "will be notified"
  waits. Completion wakeups do not reliably reach subagents: 2026-08-04
  all three run-16 agents parked on background jobs and hung for hours,
  and one background build DIED SILENTLY leaving a stale exe plus a
  corrupted incremental build (stale .objs marked up-to-date; the fix was
  deleting the Release obj dirs and recompiling). Backgrounding is for
  the top-level session only. Subagents also do NOT commit — the
  coordinator commits after verification (0fc3f29 was the second
  violation; content was correct, the rule stands).
