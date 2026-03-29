#include "mforce/patch_loader.h"
#include "mforce/wav_writer.h"
#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>

using namespace mforce;

int main(int argc, char** argv)
{
    try {
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

        // Quick stats on render buffer
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
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
