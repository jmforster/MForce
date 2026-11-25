#pragma once
#include <string>
#include <string_view>
#include "mforce/patch/fragment.h"

namespace mforce {

// A fragment as text, for the clipboard: the codec's fragment with the key
// "mforceClip": 1 beside its own, on one line. No JSON type appears here, so
// a module that may not read JSON can call these.
std::string fragment_text(const Fragment& fragment);
// The fragment a text holds. Throws std::runtime_error: "clipboard is not an
// MForce fragment" for a text that is not JSON or has no "mforceClip": 1;
// "fragment had no nodes" for one with no nodes; otherwise what the codec's
// reader refuses, in its words (an unknown node type among them).
Fragment fragment_from_text(std::string_view text);

} // namespace mforce
