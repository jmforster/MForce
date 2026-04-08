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
    float totalSeconds = part.totalBeats * 60.0f / bpm + 1.0f; // +1s tail
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    ip.instrument->render(mono.data(), frames);

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
              << part.totalBeats << " beats @ " << bpm << " bpm\n";
    std::cerr << "  peak=" << peak
              << " rms=" << rms
              << " nonzero=" << nonzero << "/" << stereo.size() << "\n";

    return 0;
}

// ---------------------------------------------------------------------------
// Build a Phrase: starting pitch, fig repeated N times descending by step, then tail figure
// ---------------------------------------------------------------------------
static Phrase build_descending_phrase(Pitch startPitch, const MelodicFigure& repFig,
                                     int reps, int stepDown,
                                     const MelodicFigure& tailFig) {
    Phrase phrase;
    phrase.startingPitch = startPitch;

    // First repetition
    phrase.add_figure(repFig);

    // Subsequent repetitions with step-down connectors
    for (int i = 1; i < reps; ++i) {
        phrase.add_figure(repFig, FigureConnector::step(stepDown));
    }

    // Tail figure continues from where we left off
    phrase.add_figure(tailFig, FigureConnector::step(0));

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
    instrument.render(mono.data(), frames);

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
                for (const auto& u : fig.units)
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
        inst->render(buf.data(), frames);
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
    conductor.chordPerformer.sloppiness = 0.3f;
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
// Classical composition mode — algorithmic melody generation
// ---------------------------------------------------------------------------
static int run_compose(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: mforce_cli --compose <patch.json> <out_prefix> [count]\n";
        return 1;
    }

    std::string patchPath = argv[2];
    std::string outPrefix = argv[3];
    int count = (argc > 4) ? std::stoi(argv[4]) : 3;

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        auto ip = load_instrument_patch(patchPath);
        ip.instrument->volume = 0.5f;
        ip.instrument->hiBoost = 0.3f;

        // Build a Piece with one empty melody Part
        Piece piece;
        piece.key = Key::get("C Major");

        Part melody;
        melody.name = "melody";
        melody.instrumentType = "melody";
        piece.add_part(std::move(melody));

        // Compose
        ClassicalComposer composer(0xC1A5'0000u + uint32_t(i) * 137);
        composer.compose(piece);

        // Perform via Conductor
        Conductor conductor;
        conductor.instruments["melody"] = ip.instrument.get();
        conductor.perform(piece);

        // Render
        float totalBeats = piece.sections[0].beats;
        float bpm = piece.sections[0].tempo;
        float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
        int frames = int(totalSeconds * float(ip.sampleRate));
        std::vector<float> mono(frames, 0.0f);
        ip.instrument->render(mono.data(), frames);

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
    ip.instrument->render(mono.data(), frames);

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
    p.mixer->render(out.data(), p.frames);

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

int main(int argc, char** argv)
{
    try {
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

        return run_patch(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
