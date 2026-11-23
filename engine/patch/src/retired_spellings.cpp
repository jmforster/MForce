#include "mforce/patch/retired_spellings.h"
#include <string>
#include <string_view>

namespace mforce {

std::string retired_key_refusal(std::string_view type, std::string_view key, RetiredAt at)
{
    // Each spelling the engine does not read: the node type (empty: any),
    // the key, where it sat, and what the converter rewrites it as.
    struct Retired { std::string_view type, key; RetiredAt at; std::string_view becomes; };
    static constexpr Retired kRetired[] = {
        {"Envelope",          "stages",     RetiredAt::Key,        "the eight stage columns"},
        {"CurveNode",         "knots",      RetiredAt::Key,        "the columns x and y"},
        {"CurveNode",         "exprKnots",  RetiredAt::Key,        "the columns exprX, exprForm, exprA and exprB"},
        {"PhasedValueSource", "stages",     RetiredAt::Key,        "sources and the columns percent, minSec, maxSec and gain"},
        {"BWLowpassFilter",   "cutoff",     RetiredAt::Key,        "cutoffFreq"},
        {"BWHighpassFilter",  "cutoff",     RetiredAt::Key,        "cutoffFreq"},
        {"SVFSource",         "cutoff",     RetiredAt::Key,        "cutoffFreq"},
        {"RepeatingSource",   "gap",        RetiredAt::Key,        "gapDuration"},
        {"MultiSource",       "weight",     RetiredAt::FeedKey,    "the array weights"},
        {"MultiSource",       "delay",      RetiredAt::FeedKey,    "the array delays"},
        {"FullPartials",      "expandRule", RetiredAt::InlineSlot, "an ExpandRule node wired by ref"},
        {"SequencePartials",  "expandRule", RetiredAt::InlineSlot, "an ExpandRule node wired by ref"},
        {"ExplicitPartials",  "expandRule", RetiredAt::InlineSlot, "an ExpandRule node wired by ref"},
        {"SoundChannel",      "inputs",     RetiredAt::NodeKey,    "the feed 'source' in params"},
        {"StereoMixer",       "inputs",     RetiredAt::NodeKey,    "the feeds 'channels' in params"},
        {"",                  "ui",         RetiredAt::TopKey,     "layout"},
        {"",                  "groups",     RetiredAt::TopKey,     "layout.groups"},
        {"",                  "_comment",   RetiredAt::TopKey,     "comment"},
        {"",                  "_provenance", RetiredAt::TopKey,    "comment"},
    };
    for (const auto& r : kRetired) {
        if ((!r.type.empty() && r.type != type) || r.key != key || r.at != at) continue;
        const char* where = at == RetiredAt::FeedKey    ? " on a feed"
                          : at == RetiredAt::InlineSlot ? " as an inline object"
                          : at == RetiredAt::NodeKey    ? " beside params"
                          : at == RetiredAt::TopKey     ? " at the top of the file"
                                                        : "";
        return "'" + std::string(key) + "'" + where
             + " is no longer read: python tools/convert_patch.py <patch> rewrites it as " + std::string(r.becomes);
    }
    return {};
}

} // namespace mforce
