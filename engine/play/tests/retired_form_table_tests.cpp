#include "mforce/testing/check.h"
#include "mforce/music/basics.h"
#include "mforce/patch/retired_spellings.h"
#include <cstdio>
#include <string>

using namespace mforce;


void run_retired_form_table_tests() {
    std::printf("retired forms: one table, by type, key and where the key sat\n");
    const std::string conv = "python tools/convert_patch.py <patch> rewrites it as ";
    struct Row { const char* type; const char* key; RetiredAt at; std::string want; };
    const Row rows[] = {
        {"Envelope", "stages", RetiredAt::Key, "'stages' is no longer read: " + conv + "the eight stage columns"},
        {"CurveNode", "knots", RetiredAt::Key, "'knots' is no longer read: " + conv + "the columns x and y"},
        {"CurveNode", "exprKnots", RetiredAt::Key,
         "'exprKnots' is no longer read: " + conv + "the columns exprX, exprForm, exprA and exprB"},
        {"PhasedValueSource", "stages", RetiredAt::Key,
         "'stages' is no longer read: " + conv + "sources and the columns percent, minSec, maxSec and gain"},
        {"BWLowpassFilter", "cutoff", RetiredAt::Key, "'cutoff' is no longer read: " + conv + "cutoffFreq"},
        {"BWHighpassFilter", "cutoff", RetiredAt::Key, "'cutoff' is no longer read: " + conv + "cutoffFreq"},
        {"SVFSource", "cutoff", RetiredAt::Key, "'cutoff' is no longer read: " + conv + "cutoffFreq"},
        {"RepeatingSource", "gap", RetiredAt::Key, "'gap' is no longer read: " + conv + "gapDuration"},
        {"MultiSource", "weight", RetiredAt::FeedKey, "'weight' on a feed is no longer read: " + conv + "the array weights"},
        {"MultiSource", "delay", RetiredAt::FeedKey, "'delay' on a feed is no longer read: " + conv + "the array delays"},
        {"FullPartials", "expandRule", RetiredAt::InlineSlot,
         "'expandRule' as an inline object is no longer read: " + conv + "an ExpandRule node wired by ref"},
        {"SequencePartials", "expandRule", RetiredAt::InlineSlot,
         "'expandRule' as an inline object is no longer read: " + conv + "an ExpandRule node wired by ref"},
        {"ExplicitPartials", "expandRule", RetiredAt::InlineSlot,
         "'expandRule' as an inline object is no longer read: " + conv + "an ExpandRule node wired by ref"},
    };
    for (const auto& r : rows) {
        const std::string got = retired_key_refusal(r.type, r.key, r.at);
        if (got != r.want) std::printf("  %s.%s: got \"%s\"\n", r.type, r.key, got.c_str());
        CHECK(got == r.want);
    }
    // The default place is a key of the node's params.
    CHECK(retired_key_refusal("BWLowpassFilter", "cutoff") == rows[4].want);
    // The same key at another place gives nothing.
    CHECK(retired_key_refusal("MultiSource", "weight").empty());
    CHECK(retired_key_refusal("MultiSource", "delay", RetiredAt::InlineSlot).empty());
    CHECK(retired_key_refusal("BWLowpassFilter", "cutoff", RetiredAt::FeedKey).empty());
    CHECK(retired_key_refusal("FullPartials", "expandRule").empty());
    CHECK(retired_key_refusal("Envelope", "stages", RetiredAt::FeedKey).empty());
    // A near miss gives nothing.
    CHECK(retired_key_refusal("BWBandpassFilter", "cutoff").empty());
    CHECK(retired_key_refusal("SegmentSource", "gap").empty());
    CHECK(retired_key_refusal("MultiSource", "stages").empty());
    CHECK(retired_key_refusal("SineSource", "weight", RetiredAt::FeedKey).empty());
    CHECK(retired_key_refusal("AdditiveSource", "formant", RetiredAt::InlineSlot).empty());
}
