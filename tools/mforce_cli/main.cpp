#include "mforce/core/denormals.h"
#include "mforce/render/patch_loader.h"
#include "mforce/render/wav_writer.h"
#include "mforce/core/source_registry.h"
#include "explore.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/music/conductor.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/music_json.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/templates.h"
#include "mforce/music/templates_json.h"
#include "mforce/music/passage_melody.h"
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
#include <chrono>

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
        apply_passage_melodies(baseTmpl);   // comp crawl: .psg melody -> phrases
        std::cout << "Loaded template: " << templatePath << "\n";
    }

    for (int i = 0; i < count; ++i) {
        // Per-part instrument patches (comp rule 2026-09-21: melody oboe1,
        // accompaniment piano_default — carried in instrumentPatch; the CLI
        // patch argument is the fallback). One loaded instance per unique
        // path, so single-patch templates render exactly as before.
        std::map<std::string, InstrumentPatch> patchByPath;
        auto patch_for = [&](const std::string& p) -> InstrumentPatch& {
            const std::string& key = p.empty() ? patchPath : p;
            auto it = patchByPath.find(key);
            if (it == patchByPath.end()) {
                auto loaded = load_instrument_patch(key);
                loaded.instrument->volume = 0.5f;
                loaded.instrument->hiBoost = 0.3f;
                it = patchByPath.emplace(key, std::move(loaded)).first;
            }
            return it->second;
        };

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

        // Perform via Conductor — lookup is by part.instrumentType, which
        // the composer sets to the template part name (composer.h).
        Conductor conductor;
        std::vector<Instrument*> instruments;   // unique, in part order
        for (const auto& partTmpl : tmpl.parts) {
            auto& partPatch = patch_for(partTmpl.instrumentPatch);
            conductor.instruments[partTmpl.name] = partPatch.instrument.get();
            if (std::find(instruments.begin(), instruments.end(),
                          partPatch.instrument.get()) == instruments.end())
                instruments.push_back(partPatch.instrument.get());
        }
        conductor.perform(piece);

        // Render each unique instrument and sum — with one instrument this
        // is exactly the old single-buffer render.
        float totalBeats = 0;
        for (auto& sec : piece.sections) totalBeats += sec.beats;
        float bpm = piece.sections[0].tempo;
        float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
        int sampleRate = patchByPath.begin()->second.sampleRate;
        int frames = int(totalSeconds * float(sampleRate));
        std::vector<float> mono(frames, 0.0f);
        std::vector<float> buf(frames);
        for (auto* inst : instruments) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            { RenderContext _ctx{sampleRate}; inst->render(_ctx, buf.data(), frames); };
            for (int k = 0; k < frames; ++k) mono[k] += buf[k];
        }

        std::vector<float> stereo(frames * 2);
        for (int j = 0; j < frames; ++j) {
            stereo[j * 2]     = mono[j];
            stereo[j * 2 + 1] = mono[j];
        }

        std::string outPath = outPrefix + "_" + std::to_string(i + 1) + ".wav";
        if (!write_wav_16le_stereo(outPath, sampleRate, stereo)) {
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

    // Timed separately from the render below: instrument+score patches
    // PRE-RENDER their voices during load, so for those the synthesis cost
    // lands here and the render timer only sees mixing. Stage-1 profiling
    // (run 8) chased a phantom 25x speedup for exactly this reason.
    auto _l0 = std::chrono::steady_clock::now();
    Patch p = load_patch_file(patchPath);
    auto _l1 = std::chrono::steady_clock::now();
    double _loadMs = std::chrono::duration<double, std::milli>(_l1 - _l0).count();

    std::vector<float> out((size_t)p.frames * 2);
    auto _t0 = std::chrono::steady_clock::now();
    { RenderContext _ctx{p.sampleRate}; p.mixer->render(_ctx, out.data(), p.frames); };
    auto _t1 = std::chrono::steady_clock::now();
    double _ms = std::chrono::duration<double, std::milli>(_t1 - _t0).count();
    double _audioMs = 1000.0 * double(p.frames) / double(p.sampleRate);
    double _sps = _ms > 0.0 ? double(p.frames) / (_ms / 1000.0) : 0.0;
    std::cerr << "  load=" << _loadMs << "ms\n";
    std::cerr << "  render=" << _ms << "ms for " << p.frames << " frames ("
              << (_sps / 1e6) << " Msamp/s, " << (_audioMs / _ms) << "x realtime)\n";
    std::cerr << "  total=" << (_loadMs + _ms) << "ms ("
              << (_audioMs / (_loadMs + _ms)) << "x realtime end-to-end)\n";

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

// Emit every registered source type's accepted JSON keys as JSON, so external
// tools (tools/lint_patches.py) can check patches against the engine's own
// truth instead of a hand-maintained list that silently drifts.
static int run_dump_descriptors(int argc, char** argv)
{
    (void)argc; (void)argv;
    register_all_sources();
    auto& reg = SourceRegistry::instance();

    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, cat] : reg.registered_types()) {
        // Instantiating is the only way to reach the virtual descriptor sets;
        // a default-constructed instance is thrown away immediately.
        std::shared_ptr<ValueSource> s;
        try { s = reg.create(name, 48000); }
        catch (const std::exception&) { continue; }
        if (!s) continue;

        nlohmann::json e = nlohmann::json::object();
        e["category"] = int(cat);
        for (const char* key : {"inputs", "params", "settings", "arrays"})
            e[key] = nlohmann::json::array();
        for (const auto& d : s->input_descriptors())  e["inputs"].push_back(d.name);
        for (const auto& d : s->param_descriptors())  e["params"].push_back(d.name);
        for (const auto& d : s->array_descriptors())  e["arrays"].push_back(d.name);
        // Configs carry their type and enum-ness: a Float config is a value
        // that happens to be expensive to change, while a Bool/enum config is
        // structural and could never take a chain. The two are worth telling
        // apart when deciding what may be driven per note.
        for (const auto& d : s->setting_descriptors()) {
            nlohmann::json c = {
                {"name", d.name},
                {"type", d.type == SettingType::Bool  ? "bool"
                       : d.type == SettingType::Int   ? "int"
                       :                               "float"},
                {"isEnum", d.enum_labels != nullptr},
                {"default", d.default_value},
                {"min", d.min_value},
                {"max", d.max_value},
            };
            e["settings"].push_back(std::move(c));
        }
        out[name] = std::move(e);
    }

    std::cout << out.dump(1) << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// --lint-template: what does the template loader silently DROP?
//
// The comp analogue of tools/lint_patches.py. A piece template is parsed into
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
            from_json(in, t);
            to_json(out, t);
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
        if (argc >= 3 && std::string(argv[1]) == "--lint-template")
            return run_lint_template(argc, argv);
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
