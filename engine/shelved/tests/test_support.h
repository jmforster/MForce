#pragma once
// Helpers the shelved tests share, and the suites one file runs from another.
#include "mforce/testing/check.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/register_builtin.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/music/music_json.h"
#include "mforce/shelved/register_shelved.h"
#include <nlohmann/json.hpp>
#include <exception>
#include <string>

using namespace mforce;


// A template read through the built-in strategies' codecs.
inline mforce::PieceTemplate template_from(const nlohmann::json& j) {
    mforce::ComposeKit kit;
    mforce::SettingsCodecs codecs;
    mforce::register_builtin(kit, codecs);
    mforce::register_shelved(kit, codecs);
    return mforce::read_template(j, codecs);
}

// A template written through the built-in strategies' codecs.
inline nlohmann::json json_of(const mforce::PieceTemplate& t) {
    mforce::ComposeKit kit;
    mforce::SettingsCodecs codecs;
    mforce::register_builtin(kit, codecs);
    mforce::register_shelved(kit, codecs);
    return mforce::write_template(t, codecs);
}

// Composes `tmpl` with every built-in strategy and voicing registered.
inline Composition compose_builtin(const PieceTemplate& tmpl) {
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    register_shelved(kit, codecs);
    return compose(tmpl, kit);
}

// One 8-beat section with two chords; a melody part with one phrase of two
// generated figures (melody part 0) and a harmony part (part 1). No part
// names a strategy or a voicing.
inline json compose_probe_json() {
    return json::parse(R"({
      "keyName": "C", "scaleName": "Major", "bpm": 100, "masterSeed": 11,
      "sections": [{"name": "Main", "beats": 8, "chordProgression": [
        {"degree": 0, "beats": 4}, {"degree": 4, "beats": 4}]}],
      "parts": [
        {"name": "melody", "role": "melody", "passages": {"Main": {
          "startingPitch": {"octave": 4, "pitch": "C"},
          "phrases": [{"name": "p",
            "startingPitch": {"octave": 4, "pitch": "C"},
            "figures": [{"source": "generate", "totalBeats": 4},
                        {"source": "generate", "totalBeats": 4}]}]}}},
        {"name": "chords", "role": "harmony", "passages": {"Main": {
          "chordConfig": {"octave": 4}}}}]
    })");
}

// True when composing `t` throws and the message holds `name`.
inline bool compose_throws_naming(const PieceTemplate& t, const ComposeKit& kit,
                                  const std::string& name) {
    try {
        compose(t, kit);
    } catch (const std::exception& e) {
        return std::string(e.what()).find(name) != std::string::npos;
    }
    return false;
}
