#pragma once
#include "mforce/patch/patch_json.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// The tool's two ways in from a patch file. Each prints what the build had
// to say on stderr, one line a warning.

struct FileInstrument {
    std::unique_ptr<mforce::PitchedInstrument> instrument;
    int sampleRate{48000};
};

inline void print_build_warnings(const std::vector<std::string>& warnings)
{
    for (const auto& text : warnings) std::cerr << "  warning: " << text << "\n";
}

// The patch's instrument, built with at least minPolyphony voices, and the
// patch's rate.
inline FileInstrument instrument_from_file(const std::string& path, int minPolyphony = 0)
{
    const mforce::Patch patch = mforce::load_patch(path);
    std::vector<std::string> warnings;
    FileInstrument file;
    file.instrument = mforce::build_instrument(patch, minPolyphony, &warnings);
    file.sampleRate = patch.sampleRate;
    print_build_warnings(warnings);
    return file;
}

// The patch as a sound to render.
inline mforce::BuiltSound sound_from_file(const std::string& path)
{
    mforce::BuiltSound sound = mforce::build_sound(mforce::load_patch(path));
    print_build_warnings(sound.warnings);
    return sound;
}
