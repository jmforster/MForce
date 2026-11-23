#include "mforce/core/denormals.h"
#include "mforce/render/wav_writer.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include "mforce/play/play.h"
#include "explore.h"
#include "patch_file.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/perform/conductor.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/register_builtin.h"
#include "mforce/shelved/register_shelved.h"
#include "mforce/music/realize.h"
#include "mforce/music/music_json.h"
#include "mforce/music/parse_util.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/compose/passage_melody.h"
#include "mforce/music/dun_parser.h"
#include <map>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>

using namespace mforce;

// ---------------------------------------------------------------------------
// What every playing mode prints and writes
// ---------------------------------------------------------------------------

// What the nodes reported during the render, one line per node id.
static void report_faults(const PlayResult& r) {
    for (const auto& f : r.faults)
        std::cerr << "[faults] " << fault_report_text(f) << "\n";
}

// The stereo frames as a 16-bit WAV; on failure the line `failText` + path.
static bool write_stereo(const std::string& path, int sampleRate, const std::vector<float>& stereo,
                         const char* failText = "Failed to write: ") {
    if (write_wav_16le_stereo(path, sampleRate, stereo)) return true;
    std::cerr << failText << path << "\n";
    return false;
}

// The levels line on stderr, with the count of nonzero samples when asked.
static void print_levels(const PlayResult& r, bool withNonzero) {
    std::cerr << "  peak=" << r.peak << " rms=" << r.rms;
    if (withNonzero) {
        const auto nonzero = std::count_if(r.stereo.begin(), r.stereo.end(), [](float s) { return s != 0.0f; });
        std::cerr << " nonzero=" << nonzero << "/" << r.stereo.size();
    }
    std::cerr << "\n";
}

// The line a scaled multi-instrument mix prints.
static void print_scale(const PlayResult& r) {
    if (r.scale != 1.0f)
        std::cout << "  normalized: mix peak " << r.mixPeak << " scaled by " << r.scale << "\n";
}

// A piece's played beats: where a section after the last would start.
static float total_beats(const Piece& piece) {
    return section_start_beat(piece.sections, piece.sections.size());
}

// ---------------------------------------------------------------------------
// Chord progression mode
// ---------------------------------------------------------------------------
static int run_chords(int argc, char** argv) {
    // mforce_cli --chords <patch.json> <out.wav> <bpm> <beats-per-chord> <chord1> <chord2> ...
    // Optional: --dict <name> --octave <n>
    if (argc < 7) {
        std::cerr << "Usage: mforce_cli --chords <patch.json> <out.wav> <bpm> <beats-per-chord> <chord>...\n"
                  << "  Options: --dict <name> --octave <n>\n"
                  << "  Example: mforce_cli --chords patches/guitar_pluck.json renders/chords.wav 120 4 C:M F:M G:7 C:M\n";
        return 1;
    }

    std::string patchPath = argv[2];
    std::string outPath   = argv[3];
    float bpm             = std::stof(argv[4]);
    float beatsPerChord   = std::stof(argv[5]);

    // Parse optional flags and collect chord tokens
    std::string dictName;
    int octave = 3;
    float volume = 1.0f;
    std::vector<std::string> chordTokens;

    for (int i = 6; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dict" && i + 1 < argc) {
            dictName = argv[++i];
        } else if (arg == "--octave" && i + 1 < argc) {
            octave = std::stoi(argv[++i]);
        } else if (arg == "--volume" && i + 1 < argc) {
            volume = std::stof(argv[++i]);
        } else {
            chordTokens.push_back(arg);
        }
    }

    if (chordTokens.empty()) {
        std::cerr << "No chords specified.\n";
        return 1;
    }

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    auto ip = instrument_from_file(patchPath);
    ip.instrument->volume = volume;

    // One section around one part of chord tokens.
    Part part;
    part.name = "chords";
    part.instrumentType = "chords";
    for (const auto& token : chordTokens)
        part.add_chord(parse_chord_token(token, octave, dictName, beatsPerChord));
    Piece piece;
    Section section;
    section.name = "chords";
    section.beats = part.totalBeats();
    section.tempo = bpm;
    piece.add_section(std::move(section));
    piece.add_part(std::move(part));

    Conductor conductor;
    conductor.chordPerformer.defaultSpreadMs = 15.0f;
    const PlayResult r = play(piece, Ensemble{{{"chords", ip.instrument.get()}}}, conductor, 1.0f);   // +1s tail
    if (!write_stereo(outPath, r.sampleRate, r.stereo, "Failed to write wav: ")) return 1;

    std::cout << "Wrote: " << outPath
              << " (" << r.frames << " frames @ " << r.sampleRate << " Hz)\n";
    std::cout << "  " << chordTokens.size() << " chords, "
              << total_beats(piece) << " beats @ " << bpm << " bpm\n";
    print_levels(r, true);
    report_faults(r);
    return 0;
}

// ---------------------------------------------------------------------------
// Build a Phrase: starting pitch, fig repeated N times descending by step, then tail figure
// ---------------------------------------------------------------------------
static Phrase build_descending_phrase(Pitch startPitch, const MelodicFigure& repFig,
                                     int reps, int /*stepDown*/,
                                     const MelodicFigure& tailFig) {
    Phrase phrase;
    phrase.startingPitch = startPitch;

    // First repetition
    phrase.add_melodic_figure(repFig);

    // Subsequent repetitions
    for (int i = 1; i < reps; ++i) {
        phrase.add_melodic_figure(repFig);
    }

    // Tail figure
    phrase.add_melodic_figure(tailFig);

    return phrase;
}


// ---------------------------------------------------------------------------
// Melody composition mode — uses compositional hierarchy (Piece/Passage/Phrase)
// ---------------------------------------------------------------------------

// One variation's piece: four phrases over random stepwise figures.
static Piece melody_piece(int variation) {
    Pitch E4 = Pitch::from_name("E", 4);
    Pitch D4 = Pitch::from_name("D", 4);

    // fig1: 3 repeated notes, quarter quarter half
    PulseSequence ps1;
    ps1.add(1.0f); ps1.add(1.0f); ps1.add(2.0f);
    StepSequence ss1;
    ss1.add(0); ss1.add(0);
    MelodicFigure fig1(ps1, ss1);

    StepGenerator stepGen(0xBE10'0000u + uint32_t(variation) * 111);

    // fig2: 4 notes, random stepwise, last pulse 4 beats
    PulseSequence ps2;
    ps2.add(1.0f); ps2.add(1.0f); ps2.add(1.0f); ps2.add(4.0f);
    MelodicFigure fig2(ps2, stepGen.random_sequence(3, 0.0f));

    // fig3: 6 notes, random stepwise, last pulse 8 beats
    PulseSequence ps3;
    ps3.add(1.0f); ps3.add(1.0f); ps3.add(1.0f);
    ps3.add(1.0f); ps3.add(1.0f); ps3.add(8.0f);
    MelodicFigure fig3(ps3, stepGen.random_sequence(5, 0.0f));

    // fig4: 8 notes, random stepwise, last pulse 8 beats
    PulseSequence ps4;
    ps4.add(1.0f); ps4.add(1.0f); ps4.add(1.0f); ps4.add(1.0f);
    ps4.add(1.0f); ps4.add(1.0f); ps4.add(1.0f); ps4.add(8.0f);
    MelodicFigure fig4(ps4, stepGen.random_sequence(7, 0.0f));

    // Build Passage: p1, p2, p1, p3
    Phrase p1 = build_descending_phrase(E4, fig1, 3, -2, fig2);
    Phrase p2 = build_descending_phrase(D4, fig1, 3, -2, fig3);
    Phrase p3 = build_descending_phrase(D4, fig1, 2, -2, fig4);
    Passage passage;
    passage.add_phrase(p1);
    passage.add_phrase(p2);
    passage.add_phrase(p1);
    passage.add_phrase(p3);

    float totalBeats = 0;
    for (const auto& ph : passage.phrases)
        for (const auto& fig : ph.figures)
            for (const auto& u : fig->units)
                totalBeats += u.duration;

    Piece piece;
    piece.key = Key::get("C Major");
    piece.add_section(Section("Main", totalBeats, 100.0f, Meter::M_4_4, Scale::get("C", "Major")));
    Part part;
    part.name = "melody";
    part.instrumentType = "pluck";
    part.passages["Main"] = std::move(passage);
    piece.add_part(std::move(part));
    return piece;
}

static int run_melody(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: mforce_cli --melody <patch.json> <out_prefix>\n";
        return 1;
    }

    std::string patchPath = argv[2];
    std::string outPrefix = argv[3];

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    for (int variation = 0; variation < 3; ++variation) {
        auto ip = instrument_from_file(patchPath);
        ip.instrument->volume = 0.5f;
        const Piece piece = melody_piece(variation);
        Conductor conductor;
        const PlayResult r = play(piece, Ensemble{{{"pluck", ip.instrument.get()}}}, conductor);

        std::string outPath = outPrefix + "_" + std::to_string(variation + 1) + ".wav";
        if (!write_stereo(outPath, r.sampleRate, r.stereo)) return 1;
        std::cout << "Wrote: " << outPath
                  << " (" << r.frames << " frames, "
                  << total_beats(piece) << " beats @ " << piece.sections.back().tempo << " bpm)\n";
        print_levels(r, false);
        report_faults(r);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Josie mode — multi-part chord progression rendering
// ---------------------------------------------------------------------------

// The four parts over the parsed chords: guitar chords, the bass on each
// chord's root an octave down, kick on beats 1 and 3, snare on 2 and the
// ands of 3 and 4.
static Piece josie_piece(const std::vector<ParsedChord>& parsed, float totalBeats, float bpm) {
    Part guitarPart;
    guitarPart.name = "guitar";
    guitarPart.instrumentType = "guitar";
    for (auto& pc : parsed) guitarPart.add_chord(pc.chord);

    Part bassPart;
    bassPart.name = "bass";
    bassPart.instrumentType = "bass";
    for (auto& pc : parsed) bassPart.add_note(pc.chord.root.note_number() - 12.0f, 0.9f, pc.chord.dur);

    Part kickPart;
    kickPart.name = "kick";
    kickPart.instrumentType = "kick";
    const float kickNN = 40.0f;  // E2 — good kick fundamental
    for (float beat = 0; beat < totalBeats; beat += 4.0f) {
        kickPart.add_note(beat, kickNN, 0.9f, 0.15f);        // beat 1
        if (beat + 2.0f < totalBeats)
            kickPart.add_note(beat + 2.0f, kickNN, 0.8f, 0.15f);  // beat 3
    }

    Part snarePart;
    snarePart.name = "snare";
    snarePart.instrumentType = "snare";
    const float snareNN = 200.0f;  // high frequency for noise character
    for (float beat = 0; beat < totalBeats; beat += 4.0f) {
        if (beat + 1.0f < totalBeats)
            snarePart.add_note(beat + 1.0f, snareNN, 0.8f, 0.12f);  // beat 2
        if (beat + 2.5f < totalBeats)
            snarePart.add_note(beat + 2.5f, snareNN, 0.6f, 0.08f);  // AND of 3
        if (beat + 3.5f < totalBeats)
            snarePart.add_note(beat + 3.5f, snareNN, 0.6f, 0.08f);  // AND of 4
    }

    Piece piece;
    piece.key = Key::get("E Minor");
    piece.add_section(Section("Josie", totalBeats, bpm, Meter::M_4_4, Scale::get("E", "Minor")));
    piece.add_part(std::move(guitarPart));
    piece.add_part(std::move(bassPart));
    piece.add_part(std::move(kickPart));
    piece.add_part(std::move(snarePart));
    return piece;
}

static int run_josie(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: mforce_cli --josie <patches_dir> <out.wav> [chord_string]\n";
        return 1;
    }

    std::string patchDir = argv[2];
    std::string outPath  = argv[3];

    // Default Josie chord string
    std::string chordStr =
        "Em7O_d Em7O_d Em7O_d "
        "O+ DM7_g5_q. CM7_g5_hh Dmu_q. Emu_hh O- "
        "Em7O_d A7_g6_w "
        "Em7O_q O+ Dmu_q Cmu_h O- "
        "Em7O_w O+ Dmu_h Emu_h O- "
        "Em7O_w Em7O_h O+ Cmu_h O- "
        "F#7#9_g6_q. B7_g5_hh Em7O_q. O+ Cmu_hh O- "
        "F#7#9_g6_q. B7b13_g5_hh Em7O_q. A7_g6_hh "
        "Am7_g6_q. O+ D7_g6_hh O- GM7_g4_q. O+ CM7_g5_hh O- "
        "F#7_g6_q F#7#9_g6_h. B7#5#9_g5_q B7#5#9_g5_q B7#5#9_g5_q B7#5#9_g5_q "
        "Em7O_d. Em7O_e";

    if (argc > 4) {
        chordStr = "";
        for (int i = 4; i < argc; ++i) {
            if (i > 4) chordStr += " ";
            chordStr += argv[i];
        }
    }

    float bpm = 132.0f;
    int baseOctave = 3;  // guitar at octave 3
    auto parsed = parse_chord_string(chordStr, baseOctave, "Default", "Josie");
    std::cout << "Parsed " << parsed.size() << " chords\n";
    float totalBeats = 0;
    for (auto& pc : parsed) totalBeats += pc.chord.dur;
    std::cout << "Total: " << totalBeats << " beats (" << (totalBeats / 4) << " bars) @ " << bpm << " bpm\n";

    // One patch per instrument, at its level in the mix.
    auto guitar = instrument_from_file(patchDir + "/guitar_pluck.json");
    auto bass   = instrument_from_file(patchDir + "/fm_bass.json");
    auto kick   = instrument_from_file(patchDir + "/kick_drum.json");
    auto snare  = instrument_from_file(patchDir + "/snare_drum.json");
    guitar.instrument->volume = 0.25f;
    bass.instrument->volume = 0.30f;
    kick.instrument->volume = 0.40f;
    snare.instrument->volume = 0.25f;

    Conductor conductor;
    conductor.chordPerformer.register_josie_figures();
    conductor.chordPerformer.defaultSpreadMs = 12.0f;
    conductor.chordPerformer.humanize = 0.3f;
    const Ensemble ensemble{{{"guitar", guitar.instrument.get()}, {"bass", bass.instrument.get()},
                             {"kick", kick.instrument.get()}, {"snare", snare.instrument.get()}}};
    const PlayResult r = play(josie_piece(parsed, totalBeats, bpm), ensemble, conductor);
    report_faults(r);
    print_scale(r);
    if (!write_stereo(outPath, r.sampleRate, r.stereo)) return 1;

    std::cout << "Wrote: " << outPath
              << " (" << r.frames << " frames, "
              << totalBeats << " beats @ " << bpm << " bpm)\n";
    print_levels(r, false);
    return 0;
}


// ---------------------------------------------------------------------------
// Classical composition mode — algorithmic melody generation
// ---------------------------------------------------------------------------

// The composed piece's harmony timeline and each part's counts, on stdout.
static void print_composed(const Piece& piece) {
    for (const auto& sec : piece.sections) {
      if (!sec.harmonyTimeline.empty()) {
        std::cout << "  Harmony for '" << sec.name << "':";
        for (const auto& seg : sec.harmonyTimeline.segments) {
          std::cout << " [" << seg.startBeat << "-" << seg.endBeat << ": "
                    << seg.progression.count() << " chords]";
        }
        std::cout << "\n";
      }
    }
    for (const auto& part : piece.parts) {
      std::cout << "  Part '" << part.name << "': "
                << part.elementSequence.size() << " events, "
                << part.passages.size() << " passages\n";
    }
}

// Composition `i` of the template, played, written and printed.
static int compose_one(const PieceTemplate& baseTmpl, const ComposeKit& kit,
                       const std::string& patchPath,
                       const std::string& outPrefix, int i) {
    // Per-part instrument patches: each part's instrumentPatch (the comp
    // templates give melody oboe1 and accompaniment piano_default), the CLI
    // patch argument the fallback. One loaded instance per unique path.
    std::map<std::string, FileInstrument> patchByPath;
    auto patch_for = [&](const std::string& p) -> FileInstrument& {
        const std::string& key = p.empty() ? patchPath : p;
        auto it = patchByPath.find(key);
        if (it == patchByPath.end()) {
            auto loaded = instrument_from_file(key);
            loaded.instrument->volume = 0.5f;
            it = patchByPath.emplace(key, std::move(loaded)).first;
        }
        return it->second;
    };

    PieceTemplate tmpl = baseTmpl;
    tmpl.masterSeed = baseTmpl.masterSeed
        ? baseTmpl.masterSeed + uint32_t(i) * 137
        : 0xC1A5'0000u + uint32_t(i) * 137;

    // Ensure at least one section and one part if template didn't specify
    if (tmpl.sections.empty())
        tmpl.sections.push_back({"Main", 32.0f});
    if (tmpl.parts.empty())
        tmpl.parts.push_back({"melody", PartRole::Melody, patchPath});

    // Ensure the first part has the instrument patch
    if (tmpl.parts[0].instrumentPatch.empty())
        tmpl.parts[0].instrumentPatch = patchPath;

    const Piece piece = compose(tmpl, kit).piece;

    // The composer sets each part's instrumentType to its template part's name.
    Ensemble ensemble;
    for (const auto& partTmpl : tmpl.parts)
        ensemble.byPart[partTmpl.name] = patch_for(partTmpl.instrumentPatch).instrument.get();
    Conductor conductor;
    const PlayResult r = play(piece, ensemble, conductor);
    report_faults(r);
    print_scale(r);

    std::string outPath = outPrefix + "_" + std::to_string(i + 1) + ".wav";
    if (!write_stereo(outPath, r.sampleRate, r.stereo)) return 1;
    std::cout << "Composed #" << (i + 1) << ": " << outPath
              << " (" << total_beats(piece) << " beats @ " << piece.sections[0].tempo << " bpm)\n";
    print_levels(r, false);

    // Save piece as JSON
    std::string jsonPath = outPrefix + "_" + std::to_string(i + 1) + ".json";
    json pieceJson = piece;
    std::ofstream jf(jsonPath);
    jf << pieceJson.dump(2);
    std::cout << "  Saved: " << jsonPath << "\n";
    print_composed(piece);
    return 0;
}

static int run_compose(int argc, char** argv) {
    // Usage: --compose <patch.json> <out_prefix> [count] [--template <template.json>]
    if (argc < 4) {
        std::cerr << "Usage: mforce_cli --compose <patch.json> <out_prefix> [count] [--template <template.json>]\n";
        return 1;
    }

    std::string patchPath = argv[2];
    std::string outPrefix = argv[3];
    int count = 1;
    std::string templatePath;

    // Parse optional args
    for (int a = 4; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--template" && a + 1 < argc) {
            templatePath = argv[++a];
        } else {
            count = std::stoi(arg);
        }
    }

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    register_shelved(kit, codecs);

    // Load template from JSON if specified
    PieceTemplate baseTmpl;
    if (!templatePath.empty()) {
        if (!std::filesystem::exists(templatePath)) {
            std::cerr << "Template file not found: " << templatePath << "\n";
            return 1;
        }
        std::ifstream tf(templatePath);
        json tj = json::parse(tf);
        baseTmpl = read_template(tj, codecs);
        apply_passage_melodies(baseTmpl);   // comp crawl: .psg melody -> phrases
        std::cout << "Loaded template: " << templatePath << "\n";
    }

    for (int i = 0; i < count; ++i)
        if (compose_one(baseTmpl, kit, patchPath, outPrefix, i) != 0) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// Play mode — load a piece JSON and render it
// ---------------------------------------------------------------------------
static int run_play(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: mforce_cli --play <piece.json> <patch.json> <out.wav>\n"
                  << "  Maps all Part instrumentTypes to the given patch.\n";
        return 1;
    }

    std::string pieceJsonPath = argv[2];
    std::string patchPath     = argv[3];
    std::string outPath       = argv[4];

    // Load piece
    std::ifstream pf(pieceJsonPath);
    if (!pf) {
        std::cerr << "Cannot open piece: " << pieceJsonPath << "\n";
        return 1;
    }
    json pj = json::parse(pf);
    Piece piece = pj.get<Piece>();
    realize_events(piece);   // a part with passages and no events gets them

    std::cout << "Loaded piece: " << piece.key.to_string()
              << ", " << piece.sections.size() << " sections, "
              << piece.parts.size() << " parts\n";

    // One instrument for every part.
    auto ip = instrument_from_file(patchPath);
    ip.instrument->volume = 0.5f;
    Ensemble ensemble;
    for (auto& part : piece.parts) ensemble.byPart[part.instrumentType] = ip.instrument.get();
    Conductor conductor;
    const PlayResult r = play(piece, ensemble, conductor);
    if (!write_stereo(outPath, r.sampleRate, r.stereo)) return 1;

    const float bpm = piece.sections.empty() ? 120.0f : piece.sections.back().tempo;
    std::cout << "Played: " << outPath
              << " (" << total_beats(piece) << " beats @ " << bpm << " bpm)\n";
    print_levels(r, false);
    report_faults(r);
    return 0;
}

// ---------------------------------------------------------------------------
// Standard patch render mode
// ---------------------------------------------------------------------------
static int run_patch(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: mforce_cli <patch.json> <out.wav>\n";
        return 1;
    }

    std::string patchPath = argv[1];
    std::string outPath   = argv[2];

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    // load= is the build and, for an instrument with a score, the score's
    // pre-render of every voice; render= is the mixer alone.
    const PlayResult r = play(load_patch(patchPath));
    print_build_warnings(r.warnings);
    const double audioMs = 1000.0 * double(r.frames) / double(r.sampleRate);
    const double sps = r.renderMs > 0.0 ? double(r.frames) / (r.renderMs / 1000.0) : 0.0;
    std::cerr << "  load=" << r.buildMs << "ms\n";
    std::cerr << "  render=" << r.renderMs << "ms for " << r.frames << " frames ("
              << (sps / 1e6) << " Msamp/s, " << (audioMs / r.renderMs) << "x realtime)\n";
    std::cerr << "  total=" << (r.buildMs + r.renderMs) << "ms ("
              << (audioMs / (r.buildMs + r.renderMs)) << "x realtime end-to-end)\n";

    if (!write_stereo(outPath, r.sampleRate, r.stereo, "Failed to write wav: ")) return 1;

    std::cout << "Wrote: " << outPath
              << " (" << r.frames << " frames @ "
              << r.sampleRate << " Hz)\n";
    print_levels(r, true);
    report_faults(r);
    return 0;
}

// ---------------------------------------------------------------------------
// DUN mode — render a .dun/.txt melody file
// ---------------------------------------------------------------------------

// The sections and each phrase's starting pitch, on stdout. A section's
// beats are its composed length on purpose, with its truncated tail beside
// it, so the report shows what was cut.
static void print_dun_sections(const Piece& piece) {
    if (piece.parts.empty()) return;
    for (int si = 0; si < int(piece.sections.size()); ++si) {
        auto& sec = piece.sections[si];
        std::cout << "  " << sec.name << ": " << sec.beats << " beats";
        if (sec.truncateTailBeats > 0)
            std::cout << " (truncate tail " << sec.truncateTailBeats << ")";
        std::cout << "\n";
        auto it = piece.parts[0].passages.find(sec.name);
        if (it != piece.parts[0].passages.end()) {
            for (int p = 0; p < int(it->second.phrases.size()); ++p) {
                std::cout << "    Phrase " << p << " start: "
                          << it->second.phrases[p].startingPitch.to_string() << "\n";
            }
        }
    }
}

static int run_dun(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: mforce_cli --dun <melody.txt> <patch.json> <out.wav> [--octave N]\n";
        return 1;
    }

    std::string dunPath   = argv[2];
    std::string patchPath = argv[3];
    std::string outPath   = argv[4];
    int octave = 4;

    for (int i = 5; i < argc; ++i) {
        if (std::string(argv[i]) == "--octave" && i + 1 < argc)
            octave = std::stoi(argv[++i]);
    }

    auto dun = load_dun(dunPath);
    int totalPhrases = 0;
    for (auto& s : dun.sections) totalPhrases += int(s.phrases.size());
    std::cout << "Loaded DUN: " << dunPath
              << " (" << dun.sections.size() << " sections, "
              << totalPhrases << " phrases, key "
              << dun.header.keyName << " " << dun.header.scaleName << ")\n";
    Piece piece = dun_to_piece(dun, octave);
    print_dun_sections(piece);

    auto ip = instrument_from_file(patchPath);
    ip.instrument->volume = 0.5f;
    Conductor conductor;
    const PlayResult r = play(piece, Ensemble{{{"melody", ip.instrument.get()}}}, conductor);
    report_faults(r);

    // Leading silence (pre-roll) so Windows audio output priming doesn't
    // eat the first note. 1s is comfortable for most drivers.
    const float prerollSeconds = 1.0f;
    const int prerollFrames = int(prerollSeconds * float(r.sampleRate));
    std::vector<float> stereo(size_t(prerollFrames) * 2, 0.0f);
    stereo.insert(stereo.end(), r.stereo.begin(), r.stereo.end());
    if (!write_stereo(outPath, r.sampleRate, stereo)) return 1;

    // The printed beats leave out each section's truncated tail.
    std::cout << "Rendered: " << outPath
              << " (" << total_beats(piece) << " beats @ " << piece.sections[0].tempo << " bpm, peak=" << r.peak << ")\n";
    return 0;
}

// Emit every registered type's accepted JSON keys as JSON, printed from the
// declarations table with each type's family from the registry, so external
// tools (tools/convert_patch.py) check patches against
// the engine's own truth instead of a hand-maintained list that drifts.
static int run_dump_descriptors(int argc, char** argv)
{
    (void)argc; (void)argv;
    const auto& reg = SourceRegistry::instance();
    nlohmann::json out = nlohmann::json::object();
    for (const auto& name : declared_types()) {
        const TypeDeclarations* d = type_declarations(name);
        const TypeTag* tag = reg.find(name);
        if (!d || !tag) continue;
        nlohmann::json e = nlohmann::json::object();
        e["family"] = family_name(tag->family);
        for (const char* key : {"inputs", "params", "settings", "arrays"})
            e[key] = nlohmann::json::array();
        for (const auto& in : d->inputs) e["inputs"].push_back(in.name);
        for (const auto& p : d->params) e["params"].push_back(p.name);
        // An enum setting and a column of indices carry their labels, in index order.
        nlohmann::json arrayLabels = nlohmann::json::object();
        for (const auto& a : d->arrays) {
            e["arrays"].push_back(a.name);
            if (!a.labels.empty()) arrayLabels[a.name] = a.labels;
        }
        if (!arrayLabels.empty()) e["arrayLabels"] = std::move(arrayLabels);
        // A setting carries its type, its default and range, and an enum its labels.
        for (const auto& s : d->settings) {
            nlohmann::json c = {
                {"name", s.name},
                {"type", s.type == SettingType::Bool   ? "bool"
                       : s.type == SettingType::Int    ? "int"
                       : s.type == SettingType::String ? "string"
                       :                                "float"},
                {"isEnum", !s.labels.empty()},
                {"default", s.defaultValue},
                {"min", s.min},
                {"max", s.max},
            };
            if (!s.labels.empty()) c["labels"] = s.labels;
            e["settings"].push_back(std::move(c));
        }
        out[name] = std::move(e);
    }

    std::cout << out.dump(1) << "\n";
    return 0;
}

// The patch files `args` name: a file as given, a folder as every *.json
// below it, sorted by path.
static std::vector<std::string> check_paths(int argc, char** argv)
{
    std::vector<std::string> paths;
    for (int a = 2; a < argc; ++a) {
        const std::filesystem::path arg = argv[a];
        if (!std::filesystem::is_directory(arg)) { paths.push_back(arg.generic_string()); continue; }
        std::vector<std::string> below;
        for (const auto& e : std::filesystem::recursive_directory_iterator(arg))
            if (e.is_regular_file() && e.path().extension() == ".json") below.push_back(e.path().generic_string());
        std::sort(below.begin(), below.end());
        paths.insert(paths.end(), below.begin(), below.end());
    }
    return paths;
}

// --check <path>...: will each patch file load? Each is read through the
// codec and built: an instrument as build_instrument builds it, a plain
// sound as build_sound does. Nothing is played or written, so an embedded
// score the player would refuse is not found. A file the reader or the
// builder refuses is printed with each line of the refusal beneath it; the
// build's warnings are not printed. Exit 1 when any file is refused.
static int run_check(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: mforce_cli --check <patch.json | folder>...  (reads and builds each file; plays and writes nothing)\n";
        return 1;
    }
    const std::vector<std::string> paths = check_paths(argc, argv);
    int refused = 0;
    for (const auto& path : paths) {
        try {
            const Patch patch = load_patch(path);
            if (patch.instrument) (void)build_instrument(patch);
            else (void)build_sound(patch);
        } catch (const std::exception& e) {
            ++refused;
            std::string message = e.what();
            if (message.rfind(path + ": ", 0) == 0) message.erase(0, path.size() + 2);
            std::cout << "REFUSED " << path << "\n";
            std::istringstream lines(message);
            for (std::string line; std::getline(lines, line);) std::cout << "  " << line << "\n";
        }
    }
    std::cout << paths.size() << " files: " << refused << " refused\n";
    return refused ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --lint-template: what does the template loader silently DROP?
//
// A piece template is parsed into
// a PieceTemplate and written straight back out; anything the author wrote
// that does not survive the round trip was ignored without a word. That class
// of bug is invisible by construction — the piece still renders, just not the
// piece that was authored. (Found this way: the section-level chordProgression
// could not be re-read after being written, and the flat authoring form
// dropped `alteration` entirely.)
//
// Reports four kinds of finding, separated because they need different
// responses — the dsp linter's first run cried wolf on 3 of 62 and the fix was
// classification, not a shorter list:
//   DROPPED   key absent from the re-serialized form, authored value is NOT a
//             default. Always suspicious.
//   DEFAULTED key absent, but the authored value equals the type's default
//             (0 / "" / false / empty). to_json omits those by design, so this
//             is usually noise — it only matters if the parser also ignored it.
//   FORM      key survives but changed shape (e.g. flat progression list in,
//             canonical {chords,pulses} out). Round-trips only if the parser
//             accepts both.
//   CHANGED   scalar present in both with a different value.
// ---------------------------------------------------------------------------
static bool lint_is_defaultish(const nlohmann::json& v)
{
    if (v.is_null()) return true;
    if (v.is_boolean()) return !v.get<bool>();
    if (v.is_number()) return std::abs(v.get<double>()) < 1e-9;
    if (v.is_string()) return v.get<std::string>().empty();
    // A container of nothing but defaults is itself a default — an all-null
    // connectors list is exactly what to_json declines to write, and calling
    // that a DROP buried 50 non-findings in the first sweep.
    if (v.is_array() || v.is_object()) {
        for (const auto& e : v) if (!lint_is_defaultish(e)) return false;
        return true;
    }
    return false;
}

struct LintFindings {
    std::vector<std::string> dropped, defaulted, form, changed;
    size_t count() const {
        return dropped.size() + defaulted.size() + form.size() + changed.size();
    }
};

static void lint_json_diff(const nlohmann::json& in, const nlohmann::json& out,
                           const std::string& path, LintFindings& f)
{
    if (in.is_object() || in.is_array()) {
        if (in.type() != out.type()) {
            f.form.push_back(path + ": " + std::string(in.type_name()) + " -> "
                             + std::string(out.type_name()));
            return;
        }
    }
    if (in.is_object()) {
        for (auto it = in.begin(); it != in.end(); ++it) {
            if (!it.key().empty() && it.key()[0] == '_') continue;  // _comment
            std::string sub = path.empty() ? it.key() : path + "." + it.key();
            if (!out.contains(it.key())) {
                // Carry the authored value: half these findings are a field
                // sitting at ITS type's default (shapeDirection 1,
                // cadenceTarget -1), which to_json omits on purpose and no
                // generic differ can know. Showing the value makes that
                // triage-able instead of guesswork.
                std::string v = it.value().dump();
                if (v.size() > 60) v = v.substr(0, 57) + "...";
                (lint_is_defaultish(it.value()) ? f.defaulted : f.dropped)
                    .push_back(sub + " = " + v);
                continue;
            }
            lint_json_diff(it.value(), out[it.key()], sub, f);
        }
        return;
    }
    if (in.is_array()) {
        if (out.size() < in.size()) {
            f.dropped.push_back(path + "[" + std::to_string(out.size()) + ".."
                                + std::to_string(in.size() - 1) + "]");
        }
        for (size_t i = 0; i < in.size() && i < out.size(); ++i) {
            lint_json_diff(in[i], out[i], path + "[" + std::to_string(i) + "]", f);
        }
        return;
    }
    if (in.is_number() && out.is_number()) {
        if (std::abs(in.get<double>() - out.get<double>()) > 1e-6)
            f.changed.push_back(path + ": " + in.dump() + " -> " + out.dump());
        return;
    }
    if (in != out) f.changed.push_back(path + ": " + in.dump() + " -> " + out.dump());
}

static int run_lint_template(int argc, char** argv)
{
    bool quiet = false;                       // --hard: hide DEFAULTED noise
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    register_shelved(kit, codecs);
    int files = 0, bad = 0;
    int nDrop = 0, nDef = 0, nForm = 0, nChg = 0;
    for (int a = 2; a < argc; ++a) {
        if (std::string(argv[a]) == "--hard") { quiet = true; continue; }
        std::string p = argv[a];
        std::ifstream f(p);
        if (!f) { std::cerr << "cannot open " << p << "\n"; return 1; }
        nlohmann::json in;
        try { in = nlohmann::json::parse(f); }
        catch (const std::exception& e) {
            std::cout << p << "\n  PARSE-ERROR " << e.what() << "\n";
            ++files; ++bad; continue;
        }

        PieceTemplate t;
        nlohmann::json out;
        try {
            t = read_template(in, codecs);
            out = write_template(t, codecs);
        } catch (const std::exception& e) {
            std::cout << p << "\n  LOAD-ERROR " << e.what() << "\n";
            ++files; ++bad; continue;
        }

        LintFindings lf;
        lint_json_diff(in, out, "", lf);
        ++files;
        nDrop += int(lf.dropped.size());   nDef  += int(lf.defaulted.size());
        nForm += int(lf.form.size());      nChg  += int(lf.changed.size());
        size_t shown = quiet ? lf.count() - lf.defaulted.size() : lf.count();
        if (shown == 0) continue;
        ++bad;
        std::cout << p << "\n";
        for (const auto& d : lf.dropped)   std::cout << "  DROPPED   " << d << "\n";
        for (const auto& d : lf.form)      std::cout << "  FORM      " << d << "\n";
        for (const auto& d : lf.changed)   std::cout << "  CHANGED   " << d << "\n";
        if (!quiet)
            for (const auto& d : lf.defaulted) std::cout << "  DEFAULTED " << d << "\n";
    }
    std::cout << "\n" << files << " templates, " << bad << " with findings: "
              << nDrop << " dropped, " << nForm << " form, " << nChg
              << " changed, " << nDef << " defaulted\n";
    return 0;
}

int main(int argc, char** argv)
{
    mforce::enable_flush_denormals();
    try {
        if (argc >= 2 && std::string(argv[1]) == "--dump-descriptors")
            return run_dump_descriptors(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--check")
            return run_check(argc, argv);
        if (argc >= 3 && std::string(argv[1]) == "--lint-template")
            return run_lint_template(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--chords")
            return run_chords(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--melody")
            return run_melody(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--josie")
            return run_josie(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--compose")
            return run_compose(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--play")
            return run_play(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--dun")
            return run_dun(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--explore")
            return run_explore(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--explore-filter")
            return run_explore_filter(argc, argv);

        return run_patch(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
