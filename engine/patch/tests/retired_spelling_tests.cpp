#include "mforce/testing/check.h"
#include "mforce/patch/retired_spellings.h"
#include <cstdio>
#include <string>

using namespace mforce;


// The table's rows for a node's own keys and for the document's: each gives
// its sentence at its place and nothing at another; a row with no type
// answers for every type.
void run_retired_spelling_table_tests() {
    std::printf("retired spellings: a node's keys and the document's, in the one table\n");
    const std::string conv = "is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
    struct Row { const char* type; const char* key; RetiredAt at; std::string want; };
    const Row rows[] = {
        {"SoundChannel", "inputs", RetiredAt::NodeKey, "'inputs' beside params " + conv + "the feed 'source' in params"},
        {"StereoMixer", "inputs", RetiredAt::NodeKey, "'inputs' beside params " + conv + "the feeds 'channels' in params"},
        {"", "ui", RetiredAt::TopKey, "'ui' at the top of the file " + conv + "layout"},
        {"", "groups", RetiredAt::TopKey, "'groups' at the top of the file " + conv + "layout.groups"},
        {"", "_comment", RetiredAt::TopKey, "'_comment' at the top of the file " + conv + "comment"},
        {"", "_provenance", RetiredAt::TopKey, "'_provenance' at the top of the file " + conv + "comment"},
    };
    for (const auto& r : rows) {
        const std::string got = retired_key_refusal(r.type, r.key, r.at);
        if (got != r.want) std::printf("  '%s'.%s: got \"%s\"\n", r.type, r.key, got.c_str());
        CHECK(got == r.want);
    }
    // A row with no type answers whatever type is asked.
    CHECK(retired_key_refusal("SoundChannel", "ui", RetiredAt::TopKey) == rows[2].want);
    CHECK(retired_key_refusal("SineSource", "_provenance", RetiredAt::TopKey) == rows[5].want);
    // The same key at another place gives nothing.
    for (RetiredAt at : {RetiredAt::Key, RetiredAt::FeedKey, RetiredAt::InlineSlot, RetiredAt::TopKey}) {
        CHECK(retired_key_refusal("SoundChannel", "inputs", at).empty());
        CHECK(retired_key_refusal("StereoMixer", "inputs", at).empty());
    }
    for (const char* key : {"ui", "groups", "_comment", "_provenance"})
        for (RetiredAt at : {RetiredAt::Key, RetiredAt::FeedKey, RetiredAt::InlineSlot, RetiredAt::NodeKey})
            CHECK(retired_key_refusal("", key, at).empty());
    // 'inputs' on another type, and on none.
    for (const char* type : {"SineSource", "MultiSource", "PerformNode", ""})
        CHECK(retired_key_refusal(type, "inputs", RetiredAt::NodeKey).empty());
    // Another key of a node, and another key of the document.
    for (const char* key : {"id", "type", "params", "dynamicPins", "comment"})
        CHECK(retired_key_refusal("SoundChannel", key, RetiredAt::NodeKey).empty());
    for (const char* key : {"sampleRate", "seconds", "instrument", "score", "graph", "layout", "comment", "inputs"})
        CHECK(retired_key_refusal("", key, RetiredAt::TopKey).empty());
}
