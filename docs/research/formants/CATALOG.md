# Vowel formant data — source catalog

First-pass survey, 2026-08-01. Goal: assemble formant tables (ideally F1–F5 with
bandwidths) for IPA monophthongs, across speech (Man / Woman / Child, with F0)
and singing (voice ranges), to eventually drive `Speech_*` / `Sing_*` patches.

**No patches built in this pass.** Nothing here is committed. All numbers in
this directory come from the cited files; nothing was invented. Where I inferred
something (e.g. IPA identity of a source's `"a"`), it is flagged as inference.

X-SAMPA is used as the ASCII vowel code by several sources below. Mapping used
throughout this document:
`i`=i `I`=ɪ `e`=e `E`=ɛ `{`=æ `a`=a `A`=ɑ `O`=ɔ `o`=o `U`=ʊ `u`=u `V`=ʌ
`3'`=ɝ (r-coloured) `y`=y `2`=ø `9`=œ `}`=ʉ `1`=ɨ `@`=ə

---

## 1. Csound "Formant Values" singing table  (= the UC xls Matt already had)

| | |
|---|---|
| File(s) | `UC_vocal_formants.xlsx` (pre-existing), `csound_singing_formants.csv` (tidy conversion I generated), `csound_appendixD_formants_uchicago_mirror.html` (downloaded page) |
| Speakers | soprano, alto, countertenor, tenor, bass — **singing**, idealized/synthetic, not individual measured talkers |
| Vowels | 5 per voice, labelled only `"a" "e" "i" "o" "u"` — **no IPA given** |
| Formants | **F1–F5** |
| Bandwidths | yes, all 5 |
| Amplitudes | yes, all 5 (dB, F1 = 0 dB reference) |
| F0 | no |
| Provenance | Csound Reference Manual, Appendix D "Formant Values": <https://csound.com/manual/misc/formants/> and <https://csound.com/docs/manual/MiscFormants.html>. Older mirror (Csound Manual 3.48b1, Table III) at <https://www.classes.cs.uchicago.edu/archive/1999/spring/CS295/Computing_Resources/Csound/CsManual3.48b1.HTML/Appendices/table3.html> — this UChicago mirror is almost certainly where the "U of Chicago" xls came from. |
| License | Csound manual, GNU FDL / LGPL-family docs. No primary citation is given in the manual for these numbers. |

**Verified identical to the xls**: soprano "a" = 800/1150/2900/3900/4950 Hz in
both. Countertenor rows exist in both. So the xls adds nothing over the Csound
table and vice-versa — one source, two copies.

**Provenance of the numbers themselves is undocumented.** The Csound manual
gives no citation, and I could not find one (searched; the manual's own
appendix carries no bibliography). The table is FOF/CHANT-lineage synthesis
data (Csound's `fof`/`fof2` opcodes), i.e. a *synthesis recipe*, not a corpus
measurement. Treat it as "known-good sounding", not as measured ground truth.

### Resolving Matt's a/e/i/o/u ambiguity (my inference, not stated by the source)

Comparing the F1/F2 pairs against the Catford male reference (§4) and Klatt (§5):

| label | tenor F1/F2 | bass F1/F2 | most consistent IPA | note |
|---|---|---|---|---|
| `a` | 650 / 1080 | 600 / 1040 | **ɑ** (or open central `a` for soprano) | soprano/alto `a` = 800/1150 sits between `a` and `ɑ` |
| `e` | 400 / 1700 | 400 / 1620 | **e** (close-mid) | F1 too low for ɛ (ɛ ≈ 530–610) |
| `i` | 290 / 1870 | 250 / 1750 | **i** | F2 depressed vs. speech `i` (≈2300); typical of sung/covered `i` |
| `o` | 400 / 800 | 400 / 750 | **o** (close-mid) | F1 too low for ɔ (ɔ ≈ 500–570) |
| `u` | 350 / 600 | 350 / 600 | **u** | |

i.e. the set is the Italian/Latin five-vowel singing set **[a ~ ɑ, e, i, o, u]**
with *close-mid* e and o, not the open-mid ɛ/ɔ. That is what you would expect
from a classical-singing table.

---

## 2. Peterson & Barney (1952) — American English, speech

| | |
|---|---|
| File(s) | `pb52_petersonbarney1952.csv` (1520 tokens), `pb52.rda` (original R object), `DERIVED_pb52_means.csv` (group means I computed from the token file) |
| Speakers | 76 talkers: 33 men (m), 28 women (w), 15 children (c). 2 repetitions each → 1520 tokens |
| Vowels | 10 monophthongs in /h_d/ frame: iy=**i** (heed), ih=**ɪ** (hid), eh=**ɛ** (head), ae=**æ** (had), aa=**ɑ** (hod), ao=**ɔ** (hawed), uh=**ʊ** (hood), uw=**u** (who'd), ah=**ʌ** (hud), er=**ɝ** (heard) |
| Formants | F1, F2, F3 only |
| Bandwidths | no |
| Amplitudes | no |
| F0 | **yes**, per token |
| Provenance | Peterson, G.E. & Barney, H.L. (1952), JASA 24(2):175–184. Data file: R package `phonTools` (Barreda), `data/pb52.rda`, <https://github.com/santiagobarreda/phonTools/blob/master/data/pb52.rda>; phonTools notes it was built from the table shipped with Praat, which in turn came from a UPenn FTP copy typed from a printed version supplied by Ignatius Mattingly (<https://www.fon.hum.uva.nl/praat/manual/Create_formant_table__Peterson___Barney_1952_.html>). |
| License | phonTools is GPL-3. Data is a 1952 published table. |

Caveat carried by Praat: Watrous (1991) relabels three vowels — Watrous uses
/e, o, ɜ/ where P&B's original notation is /ɛ, ɔ, ɜ˞/. The x-sampa labels in the
CSV follow P&B (`E`, `O`, `3'`).

Group means (from `DERIVED_pb52_means.csv`, Hz): men /i/ 267/2294/2937 at F0
136; women /i/ 310/2783/3312 at F0 231; children /i/ 360/3178/3763 at F0 270.
Mean F0 by group ≈ **men 130, women 220, children 265 Hz** — directly usable as
the `Speech_M/W/C` default F0.

---

## 3. Hillenbrand, Getty, Clark & Wheeler (1995) — American English, speech (modern P&B replication)

| | |
|---|---|
| File(s) | `hillenbrand_vowdata.dat` (1668 tokens, primary), `hillenbrand_bigdata.dat` (same but formant contour every 10% of duration), `hillenbrand_timedata.dat` (vowel start/stop/steady-state times), `hillenbrand_readme.txt`, `h95_hillenbrand1995_phonTools.csv` + `h95.rda` (phonTools' reduced copy), `DERIVED_hillenbrand1995_means.csv` (group means I computed) |
| Speakers | 139 talkers: 45 men (m), 48 women (w), 27 boys (b), 19 girls (g) — **child data split by sex**, unlike P&B |
| Vowels | 12: ae=**æ**, ah=**ɑ**, aw=**ɔ**, eh=**ɛ**, er=**ɝ**, ei=**e(ɪ)**, ih=**ɪ**, iy=**i**, oa=**o(ʊ)**, oo=**ʊ**, uh=**ʌ**, uw=**u**. Note `ei` and `oa` are diphthongal in American English — steady-state values are still given, but they are not strictly monophthongs |
| Formants | **F1–F4** at steady state; F1–F3 additionally at 20/50/80% of duration (10% steps in bigdata) |
| Bandwidths | no |
| Amplitudes | no |
| F0 | **yes**, per token |
| Provenance | Hillenbrand, J., Getty, L.A., Clark, M.J. & Wheeler, K. (1995), JASA 97:3099–3111. **The author's original page `homepages.wmich.edu/~hillenbr/voweldata.html` is dead** — it now 302s to `https://wmich.edu/` (verified). Files taken from the mirror <https://github.com/kleinschmidt/hdp-bayes-vowels/tree/master/hillenbrand-data> (`readme.txt`, `vowdata.dat`, `bigdata.dat`, `timedata.dat`). |
| License | `timedata.dat` header carries "(c) 1995 James Hillenbrand". Freely redistributed for research; no explicit open licence. The original site also offered the WAV recordings (`men.zip`/`women.zip`/`kids.zip`, 16 kHz) — **not recovered**, the mirror does not carry them. |

F4 is measured but **sparsely**: e.g. for boys' /æ/ only 18 of 27 tokens have a
non-zero F4 (0 = "not measurable"). The `n_f4_measured` column in
`DERIVED_hillenbrand1995_means.csv` records this per cell; men and women are
mostly complete (33–47 of 45/48), children are patchier.

This is the best speech source available: modern, M/W/C(+sex), F0, F1–F4,
1668 tokens, plus time-varying contours if we ever want vowel movement.

---

## 4. Catford — IPA / cardinal vowel reference

| | |
|---|---|
| File(s) | `catford2001_ipa_vowels_F1F2.csv` |
| Speakers | "average male voice", idealized — no talker population |
| Vowels | **16 IPA symbols**: i y e ø ɛ œ a ɶ ɑ ɒ ʌ ɔ ɤ o ɯ u — i.e. the 8 primary + 8 secondary cardinal vowels. This is the only source here that covers **rounded front** (y ø œ ɶ) and **unrounded back** (ɯ ɤ) vowels |
| Formants | F1, F2 only |
| Bandwidths / amplitudes / F0 | none |
| Provenance | Table reproduced in <https://en.wikipedia.org/wiki/Formant> (fetched 2026-08-01), citing Catford, J.C. (2001), *A Practical Introduction to Phonetics*, 2nd ed., OUP, p. 154. I did not have access to the book itself, so this is a second-hand reproduction. |
| License | Wikipedia text CC BY-SA; the underlying values are a published book table. |

Use this as the **IPA anchor grid**: it is the only table indexed by IPA symbol
rather than by English keyword, so it is what maps every other source into IPA
space and tells us which monophthongs have no measured data at all.

---

## 5. Klatt (1980) — synthesis parameter tables, incl. the F4/F5 convention

| | |
|---|---|
| File(s) | `Klatt1980_JASA_cascade_parallel_synthesizer.pdf` (full 25-page paper), `klatt1980_table2_vowels_RAW_EXTRACT.txt` (text extraction of Table II, symbols mangled — verify against the PDF) |
| Speakers | one idealized adult male (the author) |
| Vowels | Table II, ~14 American English vowels/diphthongs (two rows where diphthongized) |
| Formants | F1, F2, F3 per vowel — **plus fixed F4/F5/F6 defaults** |
| Bandwidths | **yes**: B1, B2, B3 per vowel; B4/B5/B6 fixed |
| Amplitudes | parallel-branch amplitudes A1–A6 exist as parameters but are not tabulated per vowel |
| F0 | parameter, not tabulated |
| Provenance | Klatt, D.H. (1980), "Software for a cascade/parallel formant synthesizer", JASA 67(3):971–995. PDF: <https://www.fon.hum.uva.nl/david/ma_ssp/doc/Klatt-1980-JAS000971.pdf> |
| License | copyrighted JASA article, mirrored by U. Amsterdam for a course. Reference only. |

Table I typical values (verbatim from the PDF, p. 976) — **this is the standard
answer to "what do I use for F4 and F5 in speech"**:

```
F4 = 3300 Hz   B4 = 250 Hz    (range 2500-4500)
F5 = 3750 Hz   B5 = 200 Hz    (range 3500-4900)
F6 = 4900 Hz   B6 = 1000 Hz
B1 = 50, B2 = 70, B3 = 110 Hz  (typical)
NFC = 5 cascaded formants (range 4-6)
```

Klatt states outright: "The fourth and fifth formant frequencies may be varied
to simulate spectral details, but this is not essential for high
intelligibility." That is the reason no speech corpus bothers to publish F5 —
see the gap analysis.

---

## 6. Other-language monophthong tables (phonTools mirrors of published means)

All from R package `phonTools` (GPL-3), <https://github.com/santiagobarreda/phonTools/tree/master/data>,
documented in <https://cran.r-project.org/web/packages/phonTools/phonTools.pdf>.
Each is a small table of **published means**, not tokens. No bandwidths, no
amplitudes, no F0 in any of them.

| File | Study | Language | Speakers | Vowels (x-sampa → IPA) | Formants |
|---|---|---|---|---|---|
| `p73_p73.csv` | Pols, Tromp & Plomp (1973), JASA 53:1093–1101 | Dutch | 50 male (means over 24 in this copy) | i ɪ e ɛ a ɑ ɔ o u **y œ ø** | F1–F3 |
| `f73_f73.csv` | Fant (1973), *Speech Sounds and Features*, MIT Press | Swedish | 50 male | i e ɛ a ɑ o u **y ʉ ø** | **F1–F4** |
| `a96_a96.csv` | Aronson, Rosenhouse, Rosenhouse & Podoshin (1996), J. Phonetics 24:283–293 | Modern Hebrew | 6 male + female | i e a o u | **F1–F4** |
| `y96_y96.csv` | Yang (1996), J. Phonetics 24:245–261 | Korean | 60 male+female | i ɨ e ɛ a o ø u y ʌ | F1–F3 |
| `t07_t07.csv` | Thomson (2007) PhD, U. Alberta | English (Edmonton) | male + female means | i ɪ e ɛ æ ɑ ʌ o ʊ u | F1–F3 |
| `b95_b95.csv` | Bradlow (1995), JASA 97:1916–1924 | Spanish | 4 male | i e a o u | F1, F2 |
| `f99_f99.csv` | Fourakis, Botinis & Katsaiti (1999), *Phonetica* 56:28–43 | Greek | 5 speakers | i e a o u | F1, F2 |

Value here: **Pols (Dutch) and Fant (Swedish) are the only measured sources in
this directory that cover the rounded front vowels y ø œ ʉ**, and Fant gives F4
for them. Everything else outside English is F1–F3 or F1–F2.

---

## 7. Sources identified but NOT obtained

- **Becker-Kristal (2010)**, *Acoustic Typology of Vowel Inventories and
  Dispersion Theory*, PhD, UCLA. Compiles 550 published studies into ~304
  inventories over 223 languages, indexed by **IPA symbol**, listing **up to the
  first 5 formants** per vowel. This is exactly the shape of data we want and is
  the largest such corpus. Two fetch attempts failed: the arXiv papers that use
  it (<https://arxiv.org/pdf/1807.02745>, Cotterell & Eisner) cite it but publish
  no data URL, and searches surface only ProQuest/ERIC
  (<https://eric.ed.gov/?id=ED526778>) and a Scribd copy — no open download.
  Worth one more targeted try later (author contact / UCLA eScholarship / the
  ACL authors' released splits).
- **Hillenbrand WAV recordings** (`men.zip`, `women.zip`, `kids.zip`, 16 kHz).
  Original host is dead; the GitHub mirror carries only the measurement files.
  If found, they would let us measure F4/F5 ourselves with high-order LPC.
- **Hawks & Miller (1995)**, "A formant bandwidth estimation procedure for vowel
  synthesis", JASA 97:1343–1344 — a regression giving bandwidth from formant
  centre frequency alone, with separate coefficients below/above 500 Hz and a
  vocal-tract-size scalar S. Praat's manual page
  (<https://www.fon.hum.uva.nl/praat/manual/Hawks___Miller__1995_.html>) is
  citation-only; the coefficients are inside the paywalled 2-page JASA note. **This
  is the right tool for synthesizing the missing bandwidths** and should be
  chased (it is 2 pages; a secondary source reproducing the formula would do).
- **eSpeak-ng vowel data** (`phsource/vwl_*`) — per-vowel formant keyframes with
  amplitudes for many languages, but stored as binary "spectral sequence" files
  produced by espeakedit, not as readable tables. Extractable only by writing a
  parser. Noted, not pursued.
- **Ladefoged / UCLA Phonetics Lab cardinal-vowel recordings** — audio, not
  tables; a fallback route to measured F3–F5 for cardinal vowels if we do our own
  LPC analysis. (UCL's PALS page was also unreachable: connection refused to
  `phon.ucl.ac.uk`.)

---

## 8. Gap analysis

**Coverage by IPA monophthong** (⬤ measured multi-speaker data, ◐ single
published table, ○ nothing):

| IPA | F1/F2 | F3 | F4 | F5 | best source |
|---|---|---|---|---|---|
| i | ⬤ | ⬤ | ⬤ | ○ | H95 (M/W/B/G + F0) |
| ɪ | ⬤ | ⬤ | ⬤ | ○ | H95 |
| e | ⬤ | ⬤ | ◐ | ○ | H95 (`ei`, diphthongal) + Fant/Pols |
| ɛ | ⬤ | ⬤ | ⬤ | ○ | H95 |
| æ | ⬤ | ⬤ | ⬤ | ○ | H95 |
| a | ◐ | ◐ | ◐ | ○ | Pols/Fant/Catford (not an AE phoneme) |
| ɑ | ⬤ | ⬤ | ⬤ | ○ | H95 |
| ɒ | ◐ | ○ | ○ | ○ | Catford F1/F2 only |
| ɔ | ⬤ | ⬤ | ⬤ | ○ | H95 (`aw`) |
| o | ⬤ | ⬤ | ◐ | ○ | H95 (`oa`, diphthongal) + Fant |
| ʊ | ⬤ | ⬤ | ⬤ | ○ | H95 |
| u | ⬤ | ⬤ | ⬤ | ○ | H95 |
| ʌ | ⬤ | ⬤ | ⬤ | ○ | H95 |
| ɝ / ɜ˞ | ⬤ | ⬤ | ⬤ | ○ | H95 (rhotic, low F3 ≈ 1700) |
| ə | ○ | ○ | ○ | ○ | nothing — derive from ʌ, or Klatt's neutral 450/1450/2450 |
| y | ◐ | ◐ | ◐ | ○ | Fant (Swedish), Pols (Dutch) |
| ø | ◐ | ◐ | ◐ | ○ | Fant, Pols |
| œ | ◐ | ◐ | ○ | ○ | Pols only |
| ɶ | ◐ | ○ | ○ | ○ | Catford F1/F2 only |
| ʉ | ◐ | ◐ | ◐ | ○ | Fant only |
| ɨ | ◐ | ◐ | ○ | ○ | Yang (Korean) |
| ɯ | ◐ | ○ | ○ | ○ | Catford F1/F2 only |
| ɤ | ◐ | ○ | ○ | ○ | Catford F1/F2 only |
| ɐ ɘ ɵ ɜ ɞ ʏ ɪ̈ ʊ̈ | ○ | ○ | ○ | ○ | nothing anywhere |

**Headline finding: F5 does not exist in any measured speech dataset.** Not P&B,
not Hillenbrand (stops at F4, and even F4 is partly unmeasurable), not any of the
phonTools tables. The only F5 numbers in circulation are (a) the Csound/CHANT
**singing** table (§1, 5 voice types × 5 vowels, and it is a synthesis recipe of
undocumented origin) and (b) Klatt's **fixed defaults** F4 3300 / F5 3750 for an
adult male. This is not an accident of searching: F4/F5 are near-invariant
speaker-characteristic resonances that carry little vowel identity, so corpora
don't publish them. Becker-Kristal (§7) is the only claimed exception and is not
open.

Consequences: for every speech vowel we will be **extrapolating F4/F5** no matter
what, and for `ɒ ɶ ɯ ɤ ə ɐ ɘ ɵ ɜ ɞ` we will be extrapolating F3 as well.
Bandwidths must be extrapolated everywhere except the Csound singing set and
Klatt's B1–B3 — Hawks & Miller (§7) is the principled way to do that.

## 9. Recommended assembly plan (one paragraph, not an implementation)

Build the grid in three layers. **Layer 1 (measured core):** take
`DERIVED_hillenbrand1995_means.csv` as the F0/F1–F4 spine for `Speech_M`,
`Speech_W`, and `Speech_C` (average the b/g rows, or keep them split — the data
supports both) across the 12 English vowels, falling back to
`DERIVED_pb52_means.csv` as a cross-check; add the non-English rounded/back
vowels from Fant (Swedish, has F4) and Pols (Dutch) rescaled to each speaker
group by the F3 or vocal-tract-length ratio implied by Hillenbrand, since those
tables are male-only. **Layer 2 (IPA completion):** for the monophthongs no
corpus covers (ɒ ɶ ɯ ɤ ə and the central set), place F1/F2 from the Catford
cardinal table and interpolate F3 from the measured neighbours in the F1/F2
plane — i.e. treat the Catford grid as the index and regress F3 on (F1,F2) over
the vowels where both are known. **Layer 3 (upper formants and bandwidths):**
give every speech vowel F5 and (where missing) F4 from Klatt's speaker-level
constants scaled per group by the same vocal-tract ratio — F4 3300 / F5 3750 for
men, roughly ×1.15 for women and ×1.25 for children, which is consistent with the
F4 means Hillenbrand does report (men ≈3500, women ≈4100, children ≈4300) — and
derive all bandwidths from Hawks & Miller once we have the coefficients, seeding
from Klatt's B1/B2/B3 = 50/70/110 until then. The singing side is separate and
already complete: ship `Sing_Soprano/Alto/CounterTenor/Tenor/Bass` × [a e i o u]
straight from `csound_singing_formants.csv` (it uniquely has all five formants
*plus* amplitudes *plus* bandwidths), label them with the IPA reading in §1, and
leave F0 to the note being sung. That yields a full `Speech_{M,W,C} × ~24 IPA
monophthongs` grid and a `Sing_{5 voices} × 5 vowels` grid, with a per-cell
provenance flag distinguishing measured / rescaled / interpolated / assumed, so
that a bad-sounding vowel can be traced back to which layer invented it.
