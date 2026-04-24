#include "mforce/render/patch_loader.h"
#include "mforce/render/wav_writer.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/music/conductor.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/music_json.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/templates.h"
#include "mforce/music/templates_json.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/dun_parser.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>

using namespace mforce;

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
    float hiBoost = 0.0f;
    std::vector<std::string> chordTokens;

    for (int i = 6; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dict" && i + 1 < argc) {
            dictName = argv[++i];
        } else if (arg == "--octave" && i + 1 < argc) {
            octave = std::stoi(argv[++i]);
        } else if (arg == "--volume" && i + 1 < argc) {
            volume = std::stof(argv[++i]);
        } else if (arg == "--hiboost" && i + 1 < argc) {
            hiBoost = std::stof(argv[++i]);
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

    // Load instrument
    auto ip = load_instrument_patch(patchPath);
    ip.instrument->volume = volume;
    ip.instrument->hiBoost = hiBoost;

    // Build Part from chord tokens
    Part part;
    part.name = "chords";
    for (const auto& token : chordTokens) {
        Chord chord = parse_chord_token(token, octave, dictName, beatsPerChord);
        part.add_chord(chord);
    }

    // Perform
    Conductor conductor;
    conductor.chordPerformer.defaultSpreadMs = 15.0f;
    conductor.perform(part, bpm, *ip.instrument);

    // Render
    float totalSeconds = part.totalBeats() * 60.0f / bpm + 1.0f; // +1s tail
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    { RenderContext _ctx{ip.sampleRate}; ip.instrument->render(_ctx, mono.data(), frames); };

    // Convert mono to stereo
    std::vector<float> stereo(frames * 2);
    for (int i = 0; i < frames; ++i) {
        stereo[i * 2]     = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }

    if (!write_wav_16le_stereo(outPath, ip.sampleRate, stereo)) {
        std::cerr << "Failed to write wav: " << outPath << "\n";
        return 1;
    }

    // Stats
    float peak = 0.0f;
    double rms = 0.0;
    int nonzero = 0;
    for (auto s : stereo) {
        if (s != 0.0f) nonzero++;
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / stereo.size());

    std::cout << "Wrote: " << outPath
              << " (" << frames << " frames @ " << ip.sampleRate << " Hz)\n";
    std::cout << "  " << chordTokens.size() << " chords, "
              << part.totalBeats() << " beats @ " << bpm << " bpm\n";
    std::cerr << "  peak=" << peak
              << " rms=" << rms
              << " nonzero=" << nonzero << "/" << stereo.size() << "\n";

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
// Render a Piece to a stereo WAV
// ---------------------------------------------------------------------------
static bool render_piece_to_wav(Piece& piece, const std::string& instrumentType,
                                PitchedInstrument& instrument,
                                int sampleRate, const std::string& outPath) {
    Conductor conductor;
    conductor.instruments[instrumentType] = &instrument;
    conductor.perform(piece);

    // Compute total duration from sections
    float totalBeats = 0;
    float bpm = 100.0f;
    for (const auto& s : piece.sections) {
        totalBeats += s.beats;
        bpm = s.tempo; // use last section's tempo for time calc
    }
    float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(sampleRate));
    std::vector<float> mono(frames, 0.0f);
    { RenderContext _ctx{sampleRate}; instrument.render(_ctx, mono.data(), frames); };

    instrument.renderedNotes.clear();

    std::vector<float> stereo(frames * 2);
    for (int i = 0; i < frames; ++i) {
        stereo[i * 2]     = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }

    if (!write_wav_16le_stereo(outPath, sampleRate, stereo))
        return false;

    float peak = 0.0f;
    double rms = 0.0;
    for (auto s : stereo) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / stereo.size());

    std::cout << "Wrote: " << outPath
              << " (" << frames << " frames, "
              << totalBeats << " beats @ " << bpm << " bpm)\n";
    std::cerr << "  peak=" << peak << " rms=" << rms << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// Melody composition mode — uses compositional hierarchy (Piece/Passage/Phrase)
// ---------------------------------------------------------------------------
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

    Pitch E4 = Pitch::from_name("E", 4);
    Pitch D4 = Pitch::from_name("D", 4);

    // fig1: 3 repeated notes, quarter quarter half
    PulseSequence ps1;
    ps1.add(1.0f); ps1.add(1.0f); ps1.add(2.0f);
    StepSequence ss1;
    ss1.add(0); ss1.add(0);
    MelodicFigure fig1(ps1, ss1);

    for (int variation = 0; variation < 3; ++variation) {
        auto ip = load_instrument_patch(patchPath);
        ip.instrument->volume = 0.5f;
        ip.instrument->hiBoost = 0.3f;

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

        // Build Phrases
        Phrase p1 = build_descending_phrase(E4, fig1, 3, -2, fig2);
        Phrase p2 = build_descending_phrase(D4, fig1, 3, -2, fig3);
        Phrase p3 = build_descending_phrase(D4, fig1, 2, -2, fig4);

        // Build Passage: p1, p2, p1, p3
        Passage passage;
        passage.add_phrase(p1);
        passage.add_phrase(p2);
        passage.add_phrase(p1);
        passage.add_phrase(p3);

        // Compute total beats
        float totalBeats = 0;
        for (const auto& ph : passage.phrases)
            for (const auto& fig : ph.figures)
                for (const auto& u : fig->units)
                    totalBeats += u.duration;

        // Build Piece
        Piece piece;
        piece.key = Key::get("C Major");

        Section section("Main", totalBeats, 100.0f, Meter::M_4_4,
                        Scale::get("C", "Major"));
        piece.add_section(std::move(section));

        Part part;
        part.name = "melody";
        part.instrumentType = "pluck";
        part.passages["Main"] = std::move(passage);
        piece.add_part(std::move(part));

        std::string outPath = outPrefix + "_" + std::to_string(variation + 1) + ".wav";
        if (!render_piece_to_wav(piece, "pluck", *ip.instrument, ip.sampleRate, outPath)) {
            std::cerr << "Failed to write: " << outPath << "\n";
            return 1;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Multi-instrument render helper
// ---------------------------------------------------------------------------
static void render_and_write(const std::vector<Instrument*>& instruments,
                             int sampleRate, float totalBeats, float bpm,
                             const std::string& outPath) {
    float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(sampleRate));

    // Render each instrument into its own buffer and mix
    std::vector<float> mix(frames, 0.0f);
    std::vector<float> buf(frames);

    for (auto* inst : instruments) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        { RenderContext _ctx{sampleRate}; inst->render(_ctx, buf.data(), frames); };
        for (int i = 0; i < frames; ++i)
            mix[i] += buf[i];
    }

    // Convert mono to stereo
    std::vector<float> stereo(frames * 2);
    for (int i = 0; i < frames; ++i) {
        stereo[i * 2]     = mix[i];
        stereo[i * 2 + 1] = mix[i];
    }

    if (!write_wav_16le_stereo(outPath, sampleRate, stereo)) {
        throw std::runtime_error("Failed to write: " + outPath);
    }

    float peak = 0.0f;
    double rms = 0.0;
    for (auto s : stereo) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / stereo.size());

    std::cout << "Wrote: " << outPath
              << " (" << frames << " frames, "
              << totalBeats << " beats @ " << bpm << " bpm)\n";
    std::cerr << "  peak=" << peak << " rms=" << rms << "\n";
}

// ---------------------------------------------------------------------------
// Josie mode — multi-part chord progression rendering
// ---------------------------------------------------------------------------
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

    // Parse chords
    auto parsed = parse_chord_string(chordStr, baseOctave, "Default", "Josie");

    std::cout << "Parsed " << parsed.size() << " chords\n";

    float totalBeats = 0;
    for (auto& pc : parsed) totalBeats += pc.chord.dur;

    std::cout << "Total: " << totalBeats << " beats (" << (totalBeats / 4) << " bars) @ " << bpm << " bpm\n";

    // Load separate patches for each instrument
    std::string guitarPath = patchDir + "/guitar_pluck.json";
    std::string bassPath   = patchDir + "/fm_bass.json";
    std::string kickPath   = patchDir + "/kick_drum.json";
    std::string snarePath  = patchDir + "/snare_drum.json";

    auto guitarPatch = load_instrument_patch(guitarPath);
    auto bassPatch   = load_instrument_patch(bassPath);
    auto kickPatch   = load_instrument_patch(kickPath);
    auto snarePatch  = load_instrument_patch(snarePath);

    guitarPatch.instrument->volume = 0.25f;
    guitarPatch.instrument->hiBoost = 0.3f;
    bassPatch.instrument->volume = 0.30f;
    kickPatch.instrument->volume = 0.40f;
    snarePatch.instrument->volume = 0.25f;

    // Build guitar chord Part
    Part guitarPart;
    guitarPart.name = "guitar";
    guitarPart.instrumentType = "guitar";
    for (auto& pc : parsed) {
        guitarPart.add_chord(pc.chord);
    }

    // Build bass Part — root note, 1 octave below chord
    Part bassPart;
    bassPart.name = "bass";
    bassPart.instrumentType = "bass";
    for (auto& pc : parsed) {
        float rootNN = pc.chord.root.note_number() - 12.0f;
        bassPart.add_note(rootNN, 0.9f, pc.chord.dur);
    }

    // Build kick Part — beats 1 and 3
    Part kickPart;
    kickPart.name = "kick";
    kickPart.instrumentType = "kick";
    float kickNN = 40.0f;  // E2 — good kick fundamental
    for (float beat = 0; beat < totalBeats; beat += 4.0f) {
        kickPart.add_note(beat, kickNN, 0.9f, 0.15f);        // beat 1
        if (beat + 2.0f < totalBeats)
            kickPart.add_note(beat + 2.0f, kickNN, 0.8f, 0.15f);  // beat 3
    }

    // Build snare Part — beat 2, AND of 3, AND of 4
    Part snarePart;
    snarePart.name = "snare";
    snarePart.instrumentType = "snare";
    float snareNN = 200.0f;  // high frequency for noise character
    for (float beat = 0; beat < totalBeats; beat += 4.0f) {
        if (beat + 1.0f < totalBeats)
            snarePart.add_note(beat + 1.0f, snareNN, 0.8f, 0.12f);  // beat 2
        if (beat + 2.5f < totalBeats)
            snarePart.add_note(beat + 2.5f, snareNN, 0.6f, 0.08f);  // AND of 3
        if (beat + 3.5f < totalBeats)
            snarePart.add_note(beat + 3.5f, snareNN, 0.6f, 0.08f);  // AND of 4
    }

    // Build Piece
    Piece piece;
    piece.key = Key::get("E Minor");

    Section section("Josie", totalBeats, bpm, Meter::M_4_4, Scale::get("E", "Minor"));
    piece.add_section(std::move(section));
    piece.add_part(std::move(guitarPart));
    piece.add_part(std::move(bassPart));
    piece.add_part(std::move(kickPart));
    piece.add_part(std::move(snarePart));

    // Register instruments and perform
    Conductor conductor;
    conductor.chordPerformer.register_josie_figures();
    conductor.chordPerformer.defaultSpreadMs = 12.0f;
    conductor.chordPerformer.humanize = 0.3f;
    conductor.instruments["guitar"] = guitarPatch.instrument.get();
    conductor.instruments["bass"]   = bassPatch.instrument.get();
    conductor.instruments["kick"]   = kickPatch.instrument.get();
    conductor.instruments["snare"]  = snarePatch.instrument.get();
    conductor.perform(piece);

    // Render and mix all instruments
    std::vector<Instrument*> allInstruments = {
        guitarPatch.instrument.get(),
        bassPatch.instrument.get(),
        kickPatch.instrument.get(),
        snarePatch.instrument.get()
    };
    render_and_write(allInstruments, guitarPatch.sampleRate, totalBeats, bpm, outPath);

    return 0;
}

// ---------------------------------------------------------------------------
// RFB test harness — one piece per RandomFigureBuilder public method.
// Each piece: 1 Section / 1 Part / 1 Passage / N phrases alternating
// [RFB-generated figure] and [1-beat rest], rendered with a single
// instrument patch. Deterministic via fixed seed unless overridden.
// ---------------------------------------------------------------------------
namespace {

// Build a single-phrase template containing one Locked figure.
PhraseTemplate make_figure_phrase(const MelodicFigure& fig, int idx) {
    PhraseTemplate ph;
    ph.name = "phrase_" + std::to_string(idx);
    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = fig;
    ph.figures.push_back(ft);
    return ph;
}

// Build a single-phrase template containing one Literal rest of given duration.
PhraseTemplate make_rest_phrase(float duration, int idx) {
    PhraseTemplate ph;
    ph.name = "rest_" + std::to_string(idx);
    FigureTemplate ft;
    ft.source = FigureSource::Literal;
    FigureTemplate::LiteralNote rest;
    rest.rest = true;
    rest.duration = duration;
    ft.literalNotes.push_back(rest);
    ph.figures.push_back(ft);
    return ph;
}

// Assemble a test piece: figures interleaved with rest phrases.
PieceTemplate make_rfb_test_piece(const std::string& methodName,
                                   const std::vector<MelodicFigure>& figures,
                                   const std::string& instrumentPatch,
                                   uint32_t masterSeed) {
    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = masterSeed;

    const float restDur = 1.0f;

    float totalBeats = 0.0f;
    for (const auto& f : figures) {
        for (const auto& u : f.units) totalBeats += u.duration;
        totalBeats += restDur;
    }
    totalBeats += 1.0f;  // tail silence

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = totalBeats;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;
    part.instrumentPatch = instrumentPatch;

    PassageTemplate passage;
    passage.name = methodName;
    passage.startingPitch = Pitch::from_name("C", 4);
    for (int i = 0; i < int(figures.size()); ++i) {
        passage.phrases.push_back(make_figure_phrase(figures[i], i));
        passage.phrases.push_back(make_rest_phrase(restDur, i));
    }
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    return tmpl;
}

// Factories — one per RFB method. Each returns the figures that the test
// piece for that method will contain, in order.
std::vector<MelodicFigure> figs_build_singleton(uint32_t seed) {
    RandomFigureBuilder rfb(seed);
    std::vector<MelodicFigure> out;
    for (float L : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
        Constraints c; c.length = L;
        out.push_back(rfb.build_singleton(c));
    }
    return out;
}

std::vector<MelodicFigure> figs_build_by_count(uint32_t seed) {
    RandomFigureBuilder rfb(seed);
    std::vector<MelodicFigure> out;
    for (int n : {2, 3, 4, 5, 6, 7, 8}) out.push_back(rfb.build_by_count(n));
    return out;
}

std::vector<MelodicFigure> figs_build_by_length(uint32_t seed) {
    RandomFigureBuilder rfb(seed);
    std::vector<MelodicFigure> out;
    for (float L : {2.0f, 3.0f, 4.0f, 6.0f, 8.0f}) out.push_back(rfb.build_by_length(L));
    return out;
}

std::vector<MelodicFigure> figs_build_by_steps(uint32_t seed) {
    RandomFigureBuilder rfb(seed);
    std::vector<std::vector<int>> patterns = {
        {0},                          // singleton
        {0, 1, 1, 1},                 // ascending scale
        {0, -1, -1, -1},              // descending scale
        {0, 2, 2},                    // triadic outline
        {0, 4, -1, -1, -1},           // leap + stepwise fill
        {0, 1, -1, 1, -1, 1, -1},     // zigzag
    };
    std::vector<MelodicFigure> out;
    for (auto& p : patterns) {
        StepSequence ss;
        for (int s : p) ss.add(s);
        out.push_back(rfb.build_by_steps(ss));
    }
    return out;
}

std::vector<MelodicFigure> figs_build_by_rhythm(uint32_t seed) {
    RandomFigureBuilder rfb(seed);
    std::vector<std::vector<float>> rhythms = {
        {1.0f, 1.0f, 1.0f, 1.0f},                                 // quarter notes
        {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f},         // eighth notes
        {0.25f, 0.25f, 0.25f, 0.25f, 1.0f, 1.0f},                 // fast -> slow
        {2.0f, 1.0f, 1.0f},                                       // half + quarters
        {0.75f, 0.25f, 0.5f, 0.5f},                               // dotted feel
    };
    std::vector<MelodicFigure> out;
    for (auto& r : rhythms) {
        PulseSequence ps;
        for (float d : r) ps.add(d);
        out.push_back(rfb.build_by_rhythm(ps));
    }
    return out;
}

std::vector<MelodicFigure> figs_build(uint32_t seed) {
    RandomFigureBuilder rfb(seed);
    std::vector<Constraints> configs(6);
    // Round-1 dispatcher's satisfies_() rejects on net/ceiling/floor but
    // the shape picks don't always honor them — so keep constraints loose
    // to reliably exercise the weighted dispatcher.
    configs[0].count = 3;
    configs[1].count = 5;
    configs[2].count = 7;
    configs[3].length = 4.0f;
    configs[4].count = 8; configs[4].defaultPulse = 0.5f;
    configs[5].count = 4; configs[5].defaultPulse = 1.0f;
    std::vector<MelodicFigure> out;
    for (auto& c : configs) out.push_back(rfb.build(c));
    return out;
}

} // namespace

static int run_test_rfb(int argc, char** argv) {
    // Usage: --test-rfb <patch.json> <out_dir> [seed]
    if (argc < 4) {
        std::cerr << "Usage: mforce_cli --test-rfb <patch.json> <out_dir> [seed]\n"
                  << "  Renders 6 pieces exercising each RandomFigureBuilder public method.\n"
                  << "  Files written: <out_dir>/rfb_<method>.{wav,json}\n";
        return 1;
    }

    std::string patchPath = argv[2];
    std::string outDir = argv[3];
    uint32_t seed = (argc > 4) ? uint32_t(std::stoul(argv[4])) : 0x7E57'0E1Fu;

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }
    std::filesystem::create_directories(outDir);

    struct TestCase {
        std::string name;
        std::vector<MelodicFigure> (*factory)(uint32_t);
    };
    std::vector<TestCase> tests = {
        {"build",            figs_build},
        {"build_by_count",   figs_build_by_count},
        {"build_by_length",  figs_build_by_length},
        {"build_by_steps",   figs_build_by_steps},
        {"build_by_rhythm",  figs_build_by_rhythm},
        {"build_singleton",  figs_build_singleton},
    };

    for (const auto& tc : tests) {
        std::vector<MelodicFigure> figures = tc.factory(seed);
        PieceTemplate tmpl = make_rfb_test_piece(tc.name, figures, patchPath, seed);

        auto ip = load_instrument_patch(patchPath);
        ip.instrument->volume = 0.5f;
        ip.instrument->hiBoost = 0.3f;

        Piece piece;
        ClassicalComposer composer(tmpl.masterSeed);
        composer.compose(piece, tmpl);

        Conductor conductor;
        for (const auto& part : piece.parts) {
            conductor.instruments[part.instrumentType] = ip.instrument.get();
        }
        conductor.perform(piece);

        float totalBeats = 0;
        for (auto& sec : piece.sections) totalBeats += sec.beats;
        float bpm = piece.sections[0].tempo;
        float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
        int frames = int(totalSeconds * float(ip.sampleRate));
        std::vector<float> mono(frames, 0.0f);
        { RenderContext _ctx{ip.sampleRate}; ip.instrument->render(_ctx, mono.data(), frames); }

        std::vector<float> stereo(frames * 2);
        for (int j = 0; j < frames; ++j) {
            stereo[j * 2]     = mono[j];
            stereo[j * 2 + 1] = mono[j];
        }

        std::string outWav = outDir + "/rfb_" + tc.name + ".wav";
        std::string outJson = outDir + "/rfb_" + tc.name + ".json";
        if (!write_wav_16le_stereo(outWav, ip.sampleRate, stereo)) {
            std::cerr << "Failed to write: " << outWav << "\n";
            return 1;
        }

        json pieceJson = piece;
        std::ofstream jf(outJson);
        jf << pieceJson.dump(2);

        float peak = 0.0f;
        double rms = 0.0;
        for (auto s : stereo) {
            float a = std::fabs(s);
            if (a > peak) peak = a;
            rms += double(s) * double(s);
        }
        rms = std::sqrt(rms / stereo.size());

        std::cout << "rfb_" << tc.name << ": " << figures.size() << " figures, "
                  << totalBeats << " beats, peak=" << peak << " rms=" << rms << "\n";
        std::cout << "  " << outWav << "\n  " << outJson << "\n";
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Classical composition mode — algorithmic melody generation
// ---------------------------------------------------------------------------
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

    // Load template from JSON if specified
    PieceTemplate baseTmpl;
    if (!templatePath.empty()) {
        if (!std::filesystem::exists(templatePath)) {
            std::cerr << "Template file not found: " << templatePath << "\n";
            return 1;
        }
        std::ifstream tf(templatePath);
        json tj = json::parse(tf);
        from_json(tj, baseTmpl);
        std::cout << "Loaded template: " << templatePath << "\n";
    }

    for (int i = 0; i < count; ++i) {
        auto ip = load_instrument_patch(patchPath);
        ip.instrument->volume = 0.5f;
        ip.instrument->hiBoost = 0.3f;

        // Use loaded template or build a default one
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

        Piece piece;
        ClassicalComposer composer(tmpl.masterSeed);
        composer.compose(piece, tmpl);

        // Perform via Conductor
        Conductor conductor;
        for (const auto& part : piece.parts) {
          conductor.instruments[part.instrumentType] = ip.instrument.get();
        }
        conductor.perform(piece);

        // Render
        float totalBeats = 0;
        for (auto& sec : piece.sections) totalBeats += sec.beats;
        float bpm = piece.sections[0].tempo;
        float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
        int frames = int(totalSeconds * float(ip.sampleRate));
        std::vector<float> mono(frames, 0.0f);
        { RenderContext _ctx{ip.sampleRate}; ip.instrument->render(_ctx, mono.data(), frames); };

        std::vector<float> stereo(frames * 2);
        for (int j = 0; j < frames; ++j) {
            stereo[j * 2]     = mono[j];
            stereo[j * 2 + 1] = mono[j];
        }

        std::string outPath = outPrefix + "_" + std::to_string(i + 1) + ".wav";
        if (!write_wav_16le_stereo(outPath, ip.sampleRate, stereo)) {
            std::cerr << "Failed to write: " << outPath << "\n";
            return 1;
        }

        float peak = 0.0f;
        double rms = 0.0;
        for (auto s : stereo) {
            float a = std::fabs(s);
            if (a > peak) peak = a;
            rms += double(s) * double(s);
        }
        rms = std::sqrt(rms / stereo.size());

        std::cout << "Composed #" << (i + 1) << ": " << outPath
                  << " (" << totalBeats << " beats @ " << bpm << " bpm)\n";
        std::cerr << "  peak=" << peak << " rms=" << rms << "\n";

        // Save piece as JSON
        std::string jsonPath = outPrefix + "_" + std::to_string(i + 1) + ".json";
        json pieceJson = piece;
        std::ofstream jf(jsonPath);
        jf << pieceJson.dump(2);
        std::cout << "  Saved: " << jsonPath << "\n";

        // Print harmony timeline if populated
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

    std::cout << "Loaded piece: " << piece.key.to_string()
              << ", " << piece.sections.size() << " sections, "
              << piece.parts.size() << " parts\n";

    // Load instrument and register for all parts
    auto ip = load_instrument_patch(patchPath);
    ip.instrument->volume = 0.5f;
    ip.instrument->hiBoost = 0.3f;

    Conductor conductor;
    for (auto& part : piece.parts) {
        conductor.instruments[part.instrumentType] = ip.instrument.get();
    }
    conductor.perform(piece);

    // Render
    float totalBeats = 0;
    float bpm = 120.0f;
    for (auto& s : piece.sections) {
        totalBeats += s.beats;
        bpm = s.tempo;
    }

    float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    { RenderContext _ctx{ip.sampleRate}; ip.instrument->render(_ctx, mono.data(), frames); };

    std::vector<float> stereo(frames * 2);
    for (int i = 0; i < frames; ++i) {
        stereo[i * 2]     = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }

    if (!write_wav_16le_stereo(outPath, ip.sampleRate, stereo)) {
        std::cerr << "Failed to write: " << outPath << "\n";
        return 1;
    }

    float peak = 0.0f;
    double rms = 0.0;
    for (auto s : stereo) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / stereo.size());

    std::cout << "Played: " << outPath
              << " (" << totalBeats << " beats @ " << bpm << " bpm)\n";
    std::cerr << "  peak=" << peak << " rms=" << rms << "\n";

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

    Patch p = load_patch_file(patchPath);

    std::vector<float> out((size_t)p.frames * 2);
    { RenderContext _ctx{p.sampleRate}; p.mixer->render(_ctx, out.data(), p.frames); };

    if (!write_wav_16le_stereo(outPath, p.sampleRate, out)) {
        std::cerr << "Failed to write wav: " << outPath << "\n";
        return 1;
    }

    float peak = 0.0f;
    double rms = 0.0;
    int nonzero = 0;
    for (auto s : out) {
        if (s != 0.0f) nonzero++;
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / out.size());

    std::cout << "Wrote: " << outPath
              << " (" << p.frames << " frames @ "
              << p.sampleRate << " Hz)\n";
    std::cerr << "  peak=" << peak
              << " rms=" << rms
              << " nonzero=" << nonzero << "/" << out.size() << "\n";

    return 0;
}

// ---------------------------------------------------------------------------
// DUN mode — render a .dun/.txt melody file
// ---------------------------------------------------------------------------
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

    // Parse DUN file
    auto dun = load_dun(dunPath);
    int totalPhrases = 0;
    for (auto& s : dun.sections) totalPhrases += int(s.phrases.size());
    std::cout << "Loaded DUN: " << dunPath
              << " (" << dun.sections.size() << " sections, "
              << totalPhrases << " phrases, key "
              << dun.header.keyName << " " << dun.header.scaleName << ")\n";

    // Build Piece
    Piece piece = dun_to_piece(dun, octave);

    // Debug: dump sections & phrase starts, note any truncation
    if (!piece.parts.empty()) {
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

    // Load instrument
    auto ip = load_instrument_patch(patchPath);
    ip.instrument->volume = 0.5f;
    ip.instrument->hiBoost = 0.3f;

    // Perform
    Conductor conductor;
    conductor.instruments["melody"] = ip.instrument.get();
    conductor.perform(piece);

    // Render (honors section truncation)
    float totalBeats = 0;
    for (auto& sec : piece.sections) {
        totalBeats += std::max(0.0f, sec.beats - sec.truncateTailBeats);
    }
    float bpm = piece.sections[0].tempo;
    float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    { RenderContext _ctx{ip.sampleRate}; ip.instrument->render(_ctx, mono.data(), frames); };

    // Leading silence (pre-roll) so Windows audio output priming doesn't
    // eat the first note. 1s is comfortable for most drivers.
    const float prerollSeconds = 1.0f;
    int prerollFrames = int(prerollSeconds * float(ip.sampleRate));
    std::vector<float> stereo((frames + prerollFrames) * 2, 0.0f);
    for (int j = 0; j < frames; ++j) {
        stereo[(prerollFrames + j) * 2]     = mono[j];
        stereo[(prerollFrames + j) * 2 + 1] = mono[j];
    }

    if (!write_wav_16le_stereo(outPath, ip.sampleRate, stereo)) {
        std::cerr << "Failed to write: " << outPath << "\n";
        return 1;
    }

    float peak = 0.0f;
    for (auto s : stereo) { float a = std::fabs(s); if (a > peak) peak = a; }
    std::cout << "Rendered: " << outPath
              << " (" << totalBeats << " beats @ " << bpm << " bpm, peak=" << peak << ")\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Smoke test for articulation realization and ornament expansion
// ---------------------------------------------------------------------------
static int test_ornaments(int /*argc*/, char** /*argv*/) {
    Part part;
    part.instrumentType = "pluck";
    float bpm = 80.0f;

    float beat = 0.0f;

    // Descending C major scale from C5 to C4:
    // C5(72), B4(71), A4(69), G4(67), F4(65), E4(64), D4(62), C4(60)
    // Diatonic interval above each note in C major (semitones):
    // C→D=2, B→C=1, A→B=2, G→A=2, F→G=2, E→F=1, D→E=2, C→D=2
    // Diatonic interval below each note:
    // C←B=1, B←A=2, A←G=2, G←F=2, F←E=1, E←D=2, D←C=2, C←B=1
    float pitches[]    = {72, 71, 69, 67, 65, 64, 62, 60};
    int semiAbove[]    = { 2,  1,  2,  2,  2,  1,  2,  2};
    int semiBelow[]    = { 1,  2,  2,  2,  1,  2,  2,  1};
    int nPitches = 8;

    // 5 descents: whole, half, quarter, eighth, sixteenth
    float durations[] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f};
    float pauseBeats = 2.0f;

    // Cycle through 4 ornament types on alternating notes
    // positions 1, 3, 5, 7 get: mordent up, mordent down, trill up, turn
    auto make_ornament = [&](int ornIdx, int noteIdx) -> Ornament {
        switch (ornIdx % 4) {
            case 0: return Mordent{1, semiAbove[noteIdx], {articulations::HammerOn{}, articulations::PullOff{}}};
            case 1: return Mordent{-1, semiBelow[noteIdx], {articulations::HammerOn{}, articulations::PullOff{}}};
            case 2: return Trill{1, semiAbove[noteIdx], {}};
            case 3: return Turn{1, semiAbove[noteIdx], semiBelow[noteIdx], {}};
            default: return Ornament{};
        }
    };

    for (float dur : durations) {
        int ornIdx = 0;
        for (int i = 0; i < nPitches; ++i) {
            if (i % 2 == 1) {
                Note n{pitches[i], 1.0f, dur, articulations::Default{},
                       make_ornament(ornIdx++, i)};
                part.elementSequence.add({beat, n});
            } else {
                part.add_note(beat, pitches[i], 1.0f, dur);
            }
            beat += dur;
        }
        beat += pauseBeats;
    }

    part.elementSequence.totalBeats = beat;

    // Render 3 versions with escalating humanization.
    // humanize is in milliseconds (max |timing offset| per play_note call).
    struct Pass { float humanize; const char* label; };
    Pass passes[] = {
        {10.0f, "h10"},
        {30.0f, "h30"},
        {50.0f, "h50"},
    };

    for (const auto& p : passes) {
        auto ip = load_instrument_patch("patches/PluckU.json");
        if (!ip.instrument) {
            std::cerr << "ERROR: could not load PluckU.json\n";
            return 1;
        }

        Conductor conductor;
        conductor.instruments["pluck"] = ip.instrument.get();
        conductor.notePerformer.humanize = p.humanize;
        conductor.perform(part, bpm, *ip.instrument);

        int noteCount = int(ip.instrument->renderedNotes.size());
        std::cout << "[" << p.label << " humanize=" << p.humanize << "] "
                  << "Rendered " << noteCount << " notes from 40 input events\n";
        if (noteCount <= 40) {
            std::cerr << "FAIL: expected more than 40 rendered notes, got " << noteCount << "\n";
            return 1;
        }

        float totalSeconds = part.totalBeats() * 60.0f / bpm + 2.0f;
        int frames = int(totalSeconds * float(ip.sampleRate));
        std::vector<float> mono(frames, 0.0f);
        { RenderContext _ctx{ip.sampleRate}; ip.instrument->render(_ctx, mono.data(), frames); };

        float peak = 0.0f;
        for (auto s : mono) {
            float a = std::fabs(s);
            if (a > peak) peak = a;
        }

        std::cout << "    Peak amplitude: " << peak << "\n";
        if (peak < 0.01f) {
            std::cerr << "FAIL: expected non-zero audio output\n";
            return 1;
        }

        std::vector<float> stereo(frames * 2);
        for (int i = 0; i < frames; ++i) {
            stereo[i * 2]     = mono[i];
            stereo[i * 2 + 1] = mono[i];
        }
        std::string outPath = std::string("renders/test_ornaments_") + p.label + ".wav";
        if (!write_wav_16le_stereo(outPath, ip.sampleRate, stereo)) {
            std::cerr << "FAIL: could not write " << outPath << "\n";
            return 1;
        }
        std::cout << "    Wrote: " << outPath << "\n";
    }

    std::cout << "PASS\n";
    return 0;
}

int main(int argc, char** argv)
{
    try {
        if (argc >= 2 && std::string(argv[1]) == "--test-ornaments")
            return test_ornaments(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--chords")
            return run_chords(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--melody")
            return run_melody(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--josie")
            return run_josie(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--compose")
            return run_compose(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--test-rfb")
            return run_test_rfb(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--play")
            return run_play(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--dun")
            return run_dun(argc, argv);

        return run_patch(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
