# Alpha Forever Modular — research notes

Researched 2026-08-06 (web only: search + page fetches). Every claim carries the URL it came from.
Claims marked **[snippet]** come from search-engine result text where the underlying page could not be
fetched directly — treat those as lower confidence.

---

## 1. What the product is, and its status

- **Node-based modular sound design environment for Windows**, delivered as a **VST 2.4 plugin for
  64-bit hosts** (not standalone, not Steam — no evidence of any Steam distribution anywhere).
  OpenGL GPU recommended. Source: official site https://www.afmodular.com/ (fetched 2026-08-06;
  page lists Features / Docs / Contact / Documentation / Node reference / Store, "Buy now!" button).
- **Price: $80**, serial-number copy protection, sold by the developer. Source: KVR product page
  https://www.kvraudio.com/product/alpha-forever-by-alpha-forever-modular
- **First release 2018** after ~3 years of development, by Hungarian brothers **Gábor Gyutai and
  Balázs Gyutai**; name reflects "never-ending development". Source: KVR developer page
  https://www.kvraudio.com/developer/alpha-forever-modular
- **Shipped update history** (news coverage):
  - Oct 2020 update (mixing focus) — https://rekkerd.org/alpha-forever-modular-updated-with-new-nodes-features-fixes/ (via https://www.dawcrash.com/category/alpha-forever/)
  - "2021/01" update, Feb 2021 — modulated ladder/SVF/1-pole filters, Plate Reverb, Pseudo Random,
    parallel patching, 2-in/2-out mode, spectrum analyzer. Source (fetched):
    https://rekkerd.org/alpha-forever-modular-2021-01-update-brings-new-modules-and-workflow-enhancements/
  - "21/02" update, Dec 2021 — https://www.dawcrash.com/category/alpha-forever/
  - **"22/01" update — the last shipped update I could confirm.** Covered by Sound On Sound
    2022-11-11 (fetched): https://www.soundonsound.com/news/alpha-forever-modular-updated and by
    dawcrash 2022-12-12: https://www.dawcrash.com/category/alpha-forever/
  - KVR's product page still lists "Latest Version 20201023_9057" (stale database entry):
    https://www.kvraudio.com/product/alpha-forever-by-alpha-forever-modular
- **Development is alive as of Sep 2025** — the primary evidence is the long KVR thread
  (466 posts, 32 pages): https://www.kvraudio.com/forum/viewtopic.php?f=23&t=494205
  - Jan 10, 2025 (dev "9b0"): 2025 priorities are **VST3 migration, preset system overhaul,
    palette redesign**; "I cannot promise, that we will work on an Apple port ever"; current code
    does not use JUCE. Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=435
  - Jan 16, 2025: "I'm working on the marketing material for the next official update", target
    **February 2025**; **development builds distributed via Discord**; Bitwig 5.3 beta
    steals key commands (shift/ctrl) breaking the GUI. Same page: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=435
  - I found **no confirmation the Feb-2025 update actually shipped** (see "Could not determine").
  - Sep 10, 2025: "yes, the plugin is windows only, and a mac port is not planned."
    Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=450
  - **[snippet]** "As of September 4, 2025, presets for the next AFM release are on the way" —
    appeared in search results (probably from https://www.facebook.com/afmodular/, which would not
    render for me). Unverified.
- **Demo/trial: removed, permanently.** July 14, 2024, dev reply to a demo request: "We decided to
  remove the demo version a long time ago, and do not plan to bring it back."
  Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=405
- **The website has been gutted.** As of 2026-08-06 the homepage and /knowledge load, but these
  Google-indexed pages all return 404: `/patches`, `/patches/audio-rate-clock`, `/devlog`,
  `/devlog/update-202201`, `/documentation`, `/store`, `/press`, `/nodes` and individual node pages
  (`/nodes/scope`, `/nodes/sine-folder`). This matches Matt's dead free-patch link. Live pages:
  https://www.afmodular.com/ and https://www.afmodular.com/knowledge (installation, UI concepts,
  mouse/keyboard reference — no DSP or patch-format docs).

## 2. The people

- **Balázs Gyutai = "9b0"** — the public/technical voice. Twitter handle is literally
  "Balázs Gyutai (@9b0)": https://twitter.com/9b0 (title via search; page not fetchable).
  - Behance (fetched): Budapest, Hungary; "professional art director in the advertising industry";
    digital arts, electronic music, sound design, motion graphics; freelance-available; website
    listed as afmodular.com. https://www.behance.net/9b0
  - Electronic musician "9b0": SoundCloud https://soundcloud.com/9b0, Spotify
    https://open.spotify.com/artist/3LWp7Y5CCVs1HN9BG06YAw. Bio (fetched): "Balázs Gyutai aka 9b0
    is an outstanding character of the Hungarian 'underground' producer generation" —
    https://www.nvc.hu/en/selected-sounds (2020 item about his return after a 4-year hiatus).
  - **[snippet]** First single "Burnout" 2006, debut album "Error" 2010; "started developing music
    softwares with his brother" in childhood — from search text around
    https://www.electronicbeats.hu/9b0-cube-video/ and https://soundcloud.com/9b0 (not fetched).
  - KVR forum account "9b0": Hungary, member since 2003, posts as the AFM developer throughout the
    thread: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=450
  - Personal YouTube channel: https://www.youtube.com/@9b0 (confirmed via YouTube oEmbed as the
    author of the standalone piano-patch video below).
- **Gábor Gyutai** — almost invisible publicly. A LinkedIn profile "Gábor Gyutai — Senior Executive,
  Project & Portfolio Management, Consulting" (Hungary, telecom/healthcare/banking clients, senior
  consultant at ABCon consulting Ltd) exists: https://www.linkedin.com/in/gabor-gyutai/ — **whether
  this is the same Gábor Gyutai is unconfirmed**. No dev-facing posts, talks, or profiles found.
- No evidence found of any *newer product* from either brother. Balázs's ongoing public activity is
  the AFM videos/forum posts (through Sep 2025) and his 9b0 music alias.

## 3. Techniques (the part Matt cares about)

### Karplus-Strong lineage
- The original KS patch shipped with the prerelease and was Balázs's own work. Jul 2022, verbatim:
  "The Karplus-Strong patch was my work actually, and it's one of the early patches provided with
  the prerelease version. This patch evolved into these presets" — polyphonic guitar, monophonic
  guitar with legato, monophonic string with legato, polyphonic strings.
  Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=345
- AFNoding 001 is "Sustaining Harp" (KS-based sustaining harp per search description):
  https://www.youtube.com/watch?v=jgoVxZ9jPIQ

### The Allpass ("Allpluss") Resonator node — their packaged KS variant (22/01 update)
- Sound On Sound (fetched): "a Karplus-Strong-inspired resonator which uses an all-pass filter in
  place of a delay, with a second filter placed in the module's feedback loop in order to simulate
  the frequency loss of a resonating string."
  https://www.soundonsound.com/news/alpha-forever-modular-updated
- **[snippet]** Parameter detail (from search-indexed text of the now-404 afmodular devlog page
  https://www.afmodular.com/devlog/update-202201): delay replaced with an **allpass diffuser**,
  plus an additional allpass in the feedback loop. Params: **Feedback** = decay length;
  **Damping** = cutoff of the lowpass inside the feedback loop (string frequency loss);
  **Stiffness** = feedback amount of the allpass filter (string stiffness / dispersion);
  **Tension** = feedback amount of the allpass diffuser, whose delay time is set by the pitch input.
- Direct relevance to MForce: this is exactly the KS + allpass-dispersion + damped-feedback shape,
  productized as one node with four musician-facing knobs.

### Piano
- **2026-08-10: third piano video found and fully analyzed** — "Synthesisizing an acoustic
  piano sound - progress" (@9b0, Apr 24 2021, 10:44, narrated):
  https://www.youtube.com/watch?v=uuebeNV-DS8
  Full structural analysis with transcript + 1080p patch frames:
  **docs/research/afpiano_2021/ANALYSIS.md**. Headline: the 2021 working patch's hammer is
  an ENVELOPED WHITE-NOISE BURST through a filter bank (contradicts the later description's
  no-noise rule); damper = in-loop feedback drop 1.0→0.82 at gate-off; 3 strings per note
  with a pitch-dependent detune curve; loop = delay → damping LP → 1P → pitch-tracked
  2nd-order AP (SVF-derived) → pitch-tracked 1st-order ZDF AP → feedback.
- Two piano videos exist (titles/channels confirmed via YouTube oEmbed; upload dates and
  descriptions were not retrievable — YouTube pages don't render for my fetcher):
  - "Alpha Forever - AFNoding 031 - Acoustic piano synthesis" on the official channel
    (@AlphaForeverModular): https://www.youtube.com/watch?v=vLf1r1uqTg4
  - "Acoustic piano - Modular synth patch in Alpha Forever Modular VST" on Balázs's personal
    channel (@9b0): https://www.youtube.com/watch?v=a57Vbgm-1Cs
- Mar 2, 2025, verbatim: "It looks like, I'm never going to give up on synthesizing a piano using
  time-domain resonators." Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=450
- **Aug 6, 2025 — the money quote for MForce**, verbatim: "I stopped patching complex scattering
  junctions and mixing matrices. Nested allpass filters gave me similar results with a fraction of
  the work." Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=450
  (My interpretation, not the source's: he moved from waveguide-network-style coupling toward
  nested-allpass resonator structures for the piano work.)

### Other technique breadcrumbs (all from the KVR thread, dev posts with video links)
- Jul 17, 2023: in-depth **resonator explanation** video. Aug–Sep 2023: rebuilding their 4-pole NL
  filter from elemental blocks (IIR teaching), **ZDF Sallen-Key from scratch**, converting it to a
  multimode SVF. Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=405
- Apr 12, 2024: **coupled string system using comb filters and exciters**; Apr 26, 2024: **bowed
  string emulation from a single oscillator + filter**. Same page:
  https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=405
- Jul 2022: a reverb patch based on **Geraint Luff's ADC 2021 talk**.
  Source: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=345
- The AFNoding tutorial series runs to at least 032 (no evidence of 033+): 001 Sustaining Harp,
  006 Organic sound design (physical modelling) //Free patch, 009 Synthwave //Free patch,
  010 Audio granulizer //Free patch, 014 Twisted envelopes //Free patch, 026 "best friend of your
  VCV", 030 modular reverb, 031 acoustic piano, 032 modular shift register. Titles from YouTube
  search results; e.g. https://www.youtube.com/watch?v=wFUBTJ3V2uU (032),
  https://www.youtube.com/watch?v=4eGN5EGPHOc (030).

### The dead free-patch link
- Several AFNoding titles literally end in "// Free patch"; the free-patch destination was
  https://www.afmodular.com/patches ("Alpha Forever Modular - Free patches and tutorials" per its
  Google-indexed title), with per-patch pages like
  https://www.afmodular.com/patches/audio-rate-clock ("Free patch // Audio rate clock").
  **Both 404 as of 2026-08-06** (fetched directly). So the dead link Matt hit is the whole /patches
  section, removed in a site restructure.
- I could not check archived copies: my fetch tooling is blocked from web.archive.org. Worth
  checking https://web.archive.org/web/2023*/afmodular.com/patches manually in a browser.

## 4. Licensing / reusability

- **Commercial, closed source.** Serial-number protection per KVR:
  https://www.kvraudio.com/product/alpha-forever-by-alpha-forever-modular. No GitHub, no source
  release, no open licensing found anywhere.
- Patch format: **[snippet]** search text (from the now-dead site docs) said "Patches can be
  copied/pasted into text editors and online text fields", implying a text-serialized patch format
  — but no format documentation was found, and the docs pages that might have covered it are gone.
- Verdict: **inspiration-only.** The published *descriptions* of the Allpass Resonator topology
  (SOS article, forum posts) are ideas, not code, and freely usable as such.

## 5. Everything else Matt would care about

- **Community**: the KVR thread is the community hub (466 posts, active Jan–Sep 2025); there is a
  **Discord** where development builds circulate (dev post Jan 16, 2025:
  https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=435). Users repeatedly compare it
  favorably to Reaktor (same page). No standalone community patch library found — the official
  /patches section was the patch library, and it is gone.
- **Harmony/sequencer**: no dedicated quantizer node exists; the dev says the **custom function
  node "can be set up easily to work as a microtuner, loading scales is not supported"** (Jan 16,
  2025: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=435). **[snippet]** The dead
  patches page advertised "a simple patch that outputs a 3 voice chord that is always adapting to a
  predefined scale using unison" and an **Euclid node** for rhythmic patterns (search-indexed text
  of https://www.afmodular.com/patches). That scale-adapting chord patch is the closest thing I
  found to Matt's "slightly harmony-aware sequencer" — it's patch-level, not an engine feature.
- **Eurorack/CV**: dev confirmed CV in/out works with a DC-coupled audio interface (2025 thread,
  https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=450).
- **Platform future**: cross-platform only after "a complete rewrite incorporating JIT compilation
  technology" (Jul 2022 dev post: https://www.kvraudio.com/forum/viewtopic.php?t=494205&start=345);
  reaffirmed Windows-only Sep 2025.
- **Why Matt "can't find anything from the last 5 years"**: news sites stopped covering it after
  the Dec 2022 update, and the website purge killed the devlog/patches pages. The activity moved to
  the KVR thread + Discord + YouTube, and continued through at least Sep 10, 2025.

## Could not determine

- Whether the announced Feb-2025 update ever shipped, and any post-22/01 version number/changelog.
- Any activity at all in **2026** (no 2026-dated evidence found anywhere).
- Whether the plugin is currently purchasable end-to-end: the homepage has a "Buy now!" button but
  /store returns 404; I did not attempt a purchase flow.
- YouTube upload dates, view counts, video descriptions, and comments (YouTube pages and the
  channel page would not render for my fetcher; only oEmbed title/author worked).
- Archived copies of the /patches free-patch pages (web.archive.org blocked for my tooling).
- Whether the LinkedIn "Gábor Gyutai" (senior consultant) is the AFM co-developer.
- Which brother does what (DSP vs UI vs business) — Balázs claims the original KS patch and fronts
  all technical communication; Gábor's role is publicly undocumented.
- Current state of the 9b0 music alias (SoundCloud/Spotify pages would not render).
