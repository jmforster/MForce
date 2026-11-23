#include "mforce/testing/check.h"
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/perform/embedded_score.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;


// A note of the common shape: start and duration in seconds, pitch, velocity, hold.
static PerformedNote note_at(float start, float duration, float noteNumber, float velocity = 0.8f,
                             bool hold = false) {
    return {.start = start, .duration = duration, .noteNumber = noteNumber, .velocity = velocity, .hold = hold};
}

// Whether two notes agree in every field but the curve.
static bool same_note(const PerformedNote& a, const PerformedNote& b) {
    return a.start == b.start && a.duration == b.duration && a.noteNumber == b.noteNumber
        && a.velocity == b.velocity && a.onset == b.onset && a.hold == b.hold;
}

// perform_embedded_score: phrased notes copied across with hold and onset;
// hold dropped, with its warning, on a class that does not sustain; an
// unknown onset name warned once; score order kept when phrased and
// unphrased notes mix; the score's end.
void run_perform_embedded_score_tests() {
    std::printf("perform_embedded_score: phrased copies, hold dropped, onsets warned once, order, the end\n");
    const InstrumentClass sustaining{.sustaining = true, .onsets = {"tongue"}};
    const InstrumentClass plucked{.sustaining = false, .onsets = {"tongue"}};
    const char* line = R"([{"note":60,"time":0,"duration":0.5,"hold":true,"onset":"tongue"},{"note":62,"duration":0.5}])";
    {
        const EmbeddedScore s = perform_embedded_score(line, sustaining);
        PerformedNote first = note_at(0.0f, 0.5f, 60.0f, 0.8f, true);
        first.onset = 1;
        CHECK(s.performance.notes.size() == 2 && s.warnings.empty() && s.performance.hits.empty());
        CHECK(s.performance.notes.size() == 2 && same_note(s.performance.notes[0], first)
              && same_note(s.performance.notes[1], note_at(0.5f, 0.5f, 62.0f)));
        CHECK(s.endSeconds == 1.0);
    }
    {
        const EmbeddedScore s = perform_embedded_score(line, plucked);
        CHECK(s.performance.notes.size() == 2 && !s.performance.notes[0].hold && s.performance.notes[0].onset == 1);
        CHECK(s.warnings == std::vector<std::string>{
            "[score] \"hold\" ignored: this patch's instrument block is not marked \"sustaining\""});
    }
    {
        const EmbeddedScore s = perform_embedded_score(
            R"([{"note":60,"time":0,"duration":0.5,"onset":"pluck"},{"note":62,"duration":0.5,"onset":"pluck"}])", sustaining);
        CHECK(s.performance.notes.size() == 2 && s.performance.notes[0].onset == 0 && s.performance.notes[1].onset == 0);
        CHECK(s.warnings == std::vector<std::string>{
            "[onset] name 'pluck' is not in this instrument's onsets[] vocabulary — it will never fire"});
    }
    {
        const EmbeddedScore s = perform_embedded_score(
            R"([{"note":60,"time":0,"duration":1},{"note":62,"time":1,"duration":1,"onset":"tongue"},{"note":64,"time":2.5,"duration":1}])",
            sustaining);
        const PerformedNote want[] = {note_at(0.0f, 1.0f, 60.0f), note_at(1.0f, 1.0f, 62.0f), note_at(2.5f, 1.0f, 64.0f)};
        bool order = s.performance.notes.size() == 3;
        for (size_t k = 0; order && k < 3; ++k)
            order = s.performance.notes[k].start == want[k].start && s.performance.notes[k].noteNumber == want[k].noteNumber;
        CHECK(order);
        CHECK(s.performance.notes.size() == 3 && s.performance.notes[1].onset == 1 && s.endSeconds == 3.5);
    }
}
