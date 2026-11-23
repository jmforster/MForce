#include "mforce/testing/check.h"
#include "mforce/music/basics.h"
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace mforce;


// The number of tones init_pitches voices for a definition: the root unless
// omitted, and every interval that is not a unison.
static int tone_count(const ChordDef& def) {
    int n = def.omitRoot ? 0 : 1;
    for (const auto& name : def.intervals)
        if (Interval::get(name).semitones >= 0.01f) ++n;
    return n;
}

// True when every inversion and spreads 0 to 2 of `def` voice one pitch per tone.
static bool voices_every_tone(const ChordDef& def) {
    const int n = tone_count(def);
    bool ok = true;
    for (int inv = 0; inv < std::max(n, 1); ++inv) {
        for (int sp = 0; sp <= 2; ++sp) {
            Chord c;
            c.root = Pitch::from_name("C", 4);
            c.def = &def;
            c.inversion = inv;
            c.spread = sp;
            c.init_pitches();
            ok = ok && c.pitch_count() == n;
        }
    }
    return ok;
}

void run_init_pitches_tests() {
    std::printf("Chord::init_pitches: every dictionary chord returns; the pins hold\n");
    bool all = true;
    for (const auto& def : ChordDef::all()) all = all && voices_every_tone(def);
    for (const char* dict : {"Canonic", "Default", "Guitar-Bar-6", "g6", "Guitar-Bar-5",
                             "g5", "Guitar-Bar-4", "g4", "Guitar-Alt", "Piano"}) {
        for (const auto& [name, def] : ChordDictionary::get(dict).chords)
            all = all && voices_every_tone(def);
    }
    CHECK(all);

    // Note numbers captured from the code before the doubling rule, root C4.
    struct Pin { const char* type; int inv, spread; std::vector<float> nns; };
    const Pin pins[] = {
        {"Major", 0, 0, {48, 52, 55}}, {"Major", 0, 1, {48, 55, 64}}, {"Major", 0, 2, {48, 64, 79}},
        {"Major", 1, 0, {52, 55, 60}}, {"Major", 1, 1, {52, 60, 67}}, {"Major", 1, 2, {52, 67, 84}},
        {"Major", 2, 0, {55, 60, 64}}, {"Major", 2, 1, {55, 64, 72}}, {"Major", 2, 2, {55, 72, 88}},
        {"7", 0, 0, {48, 52, 55, 58}}, {"7", 0, 1, {48, 55, 64, 70}}, {"7", 0, 2, {48, 58, 67, 76}},
        {"7", 1, 0, {52, 55, 58, 60}}, {"7", 1, 1, {52, 58, 67, 72}}, {"7", 1, 2, {52, 60, 70, 79}},
        {"7", 2, 0, {55, 58, 60, 64}}, {"7", 2, 1, {55, 60, 70, 76}}, {"7", 2, 2, {55, 64, 72, 82}},
        {"7", 3, 0, {58, 60, 64, 67}}, {"7", 3, 1, {58, 64, 72, 79}}, {"7", 3, 2, {58, 67, 76, 84}},
    };
    bool pinned = true;
    for (const auto& pin : pins) {
        const Chord c = Chord::create("C", 4, pin.type, 1.0f, pin.inv, pin.spread);
        std::vector<float> nns;
        for (const auto& p : c.pitches) nns.push_back(p.note_number());
        pinned = pinned && nns == pin.nns;
    }
    CHECK(pinned);
}
