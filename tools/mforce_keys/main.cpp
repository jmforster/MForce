//
// mforce_keys — interactive keyboard for MForce instruments
//
// Keys:  q w e r t y u i o p [ ]   = white keys C4..G5
//        2 3   5 6 7   9 0   =     = black keys
//
// G/H = octave down/up, B/N = halve/double duration
// Z = cycle instrument, ESC = quit
//

#define NOMINMAX   // prevent windows.h from defining min/max macros
#include "mforce/patch_loader.h"
#include "mforce/instrument.h"
#include "mforce/equal_temperament.h"
#include "mforce/dsp_value_source.h"

#include <windows.h>
#include <mmsystem.h>
#include <conio.h>

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace mforce;

// ---------------------------------------------------------------------------
// Audio output via waveOut double-buffering
// ---------------------------------------------------------------------------

static constexpr int SAMPLE_RATE = 48000;
static constexpr int MIX_SECONDS = 30;
static constexpr int MIX_SIZE = SAMPLE_RATE * MIX_SECONDS;
static constexpr int AUDIO_BUF_SAMPLES = 2048;
static constexpr int NUM_AUDIO_BUFS = 4;

static float g_mixBuffer[MIX_SIZE] = {};
static std::atomic<int> g_readPos{0};
static int g_writePos = 0;

static HWAVEOUT g_waveOut = nullptr;
static WAVEHDR  g_waveHdrs[NUM_AUDIO_BUFS] = {};
static int16_t  g_audioBufs[NUM_AUDIO_BUFS][AUDIO_BUF_SAMPLES * 2] = {}; // stereo

static void fill_audio_buffer(WAVEHDR* hdr, int bufIdx) {
    int rp = g_readPos.load();
    auto* out = g_audioBufs[bufIdx];

    for (int i = 0; i < AUDIO_BUF_SAMPLES; ++i) {
        float s = g_mixBuffer[rp % MIX_SIZE];
        g_mixBuffer[rp % MIX_SIZE] = 0.0f; // consume
        rp++;

        s = std::clamp(s, -1.0f, 1.0f);
        int16_t v = int16_t(s < 0 ? s * 32768.0f : s * 32767.0f);
        out[i * 2]     = v;  // L
        out[i * 2 + 1] = v;  // R (mono → stereo)
    }

    g_readPos.store(rp);

    hdr->lpData = reinterpret_cast<LPSTR>(out);
    hdr->dwBufferLength = AUDIO_BUF_SAMPLES * 2 * sizeof(int16_t);
    hdr->dwFlags = 0;

    waveOutPrepareHeader(g_waveOut, hdr, sizeof(WAVEHDR));
    waveOutWrite(g_waveOut, hdr, sizeof(WAVEHDR));
}

static void CALLBACK wave_callback(HWAVEOUT, UINT msg, DWORD_PTR, DWORD_PTR param1, DWORD_PTR) {
    if (msg == WOM_DONE) {
        auto* hdr = reinterpret_cast<WAVEHDR*>(param1);
        waveOutUnprepareHeader(g_waveOut, hdr, sizeof(WAVEHDR));

        int idx = int(hdr - g_waveHdrs);
        fill_audio_buffer(hdr, idx);
    }
}

static bool init_audio() {
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    MMRESULT res = waveOutOpen(&g_waveOut, WAVE_MAPPER, &fmt,
                               (DWORD_PTR)wave_callback, 0, CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        std::cerr << "waveOutOpen failed: " << res << "\n";
        return false;
    }

    // Prime all buffers
    for (int i = 0; i < NUM_AUDIO_BUFS; ++i) {
        g_waveHdrs[i] = {};
        fill_audio_buffer(&g_waveHdrs[i], i);
    }

    return true;
}

static void shutdown_audio() {
    if (g_waveOut) {
        waveOutReset(g_waveOut);
        for (int i = 0; i < NUM_AUDIO_BUFS; ++i)
            waveOutUnprepareHeader(g_waveOut, &g_waveHdrs[i], sizeof(WAVEHDR));
        waveOutClose(g_waveOut);
        g_waveOut = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Note rendering into mix buffer
// ---------------------------------------------------------------------------

static void render_note(Instrument::VoiceGraph& vg, float noteNum, float vel, float dur) {
    float freq = note_to_freq(noteNum);
    int samples = int(dur * SAMPLE_RATE);

    if (vg.params.count("frequency"))
        vg.params["frequency"]->set(freq);

    vg.source->prepare(samples);

    int wp = g_writePos;
    for (int i = 0; i < samples; ++i) {
        g_mixBuffer[(wp + i) % MIX_SIZE] += vg.source->next() * vel;
    }
    // Don't advance writePos — overlapping notes accumulate at same position
}

// Advance write position to "now" (keep in sync with read)
static void sync_write_pos() {
    g_writePos = g_readPos.load();
}

// ---------------------------------------------------------------------------
// Key → note mapping
// ---------------------------------------------------------------------------

struct KeyMap {
    int key;
    int semitone; // semitones above C
};

// White keys: q=C w=D e=E r=F t=G y=A u=B  i=C' o=D' p=E' [=F' ]=G'
// Black keys: 2=C# 3=D#  5=F# 6=G# 7=A#  9=C#' 0=D#'  ==F#'
static const KeyMap g_keyMap[] = {
    {'q',  0}, {'w',  2}, {'e',  4}, {'r',  5}, {'t',  7}, {'y',  9}, {'u', 11},
    {'i', 12}, {'o', 14}, {'p', 16}, {'[', 17}, {']', 19},
    {'2',  1}, {'3',  3}, {'5',  6}, {'6',  8}, {'7', 10},
    {'9', 13}, {'0', 15}, {'=', 18},
};

static int find_semitone(int key) {
    for (auto& km : g_keyMap)
        if (km.key == key) return km.semitone;
    return -1;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    try {
        // Default patches
        std::vector<std::string> patchFiles;
        if (argc > 1) {
            for (int i = 1; i < argc; ++i)
                patchFiles.push_back(argv[i]);
        } else {
            patchFiles = {
                "patches/inst_melody_test.json",
                "patches/inst_chord_test.json",
                "patches/inst_fm_bell_melody_test.json"
            };
        }

        // Load instruments
        struct LoadedInstrument {
            std::string name;
            std::vector<Instrument::VoiceGraph> voices;
            int nextVoice{0};
        };

        std::vector<LoadedInstrument> instruments;

        for (auto& path : patchFiles) {
            // Parse the patch to get the graph + instrument definition
            std::ifstream f(path, std::ios::binary);
            if (!f) { std::cerr << "Cannot open: " << path << "\n"; continue; }
            std::ostringstream ss;
            ss << f.rdbuf();
            auto root = nlohmann::json::parse(ss.str());

            if (!root.contains("instrument")) {
                std::cerr << "Skipping " << path << " (no instrument section)\n";
                continue;
            }

            // We need to build voice graphs. Reuse the patch loader by loading as Patch
            // then extracting the instrument. But that's complex. Instead, just load
            // the patch normally — the Instrument is in the mixer's channel source.
            Patch patch = load_patch_file(path);

            // The first channel's source should be the Instrument
            if (patch.mixer->channels.empty()) continue;
            auto* inst = dynamic_cast<Instrument*>(patch.mixer->channels[0].source.get());
            if (!inst) { std::cerr << "Not an instrument patch: " << path << "\n"; continue; }

            LoadedInstrument li;
            li.name = path;
            li.voices = std::move(inst->voicePool);
            instruments.push_back(std::move(li));

            std::cerr << "Loaded: " << path << " (" << instruments.back().voices.size() << " voices)\n";
        }

        if (instruments.empty()) {
            std::cerr << "No instruments loaded!\n";
            return 1;
        }

        if (!init_audio()) return 1;

        int currentInst = 0;
        int octave = 0;        // offset in octaves from middle C
        float duration = 0.4f; // note duration in seconds
        float velocity = 0.8f;

        std::cerr << "\n=== MForce Keys ===\n";
        std::cerr << "White: q w e r t y u i o p [ ]\n";
        std::cerr << "Black: 2 3   5 6 7   9 0   =\n";
        std::cerr << "G/H=octave  B/N=duration  Z=instrument  ESC=quit\n";
        std::cerr << "\nInstrument: " << instruments[currentInst].name
                  << "  Oct=" << octave << "  Dur=" << duration << "\n";

        while (true) {
            int ch = _getch();

            // ESC
            if (ch == 27) break;

            // Lowercase for letter keys
            if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';

            // Special keys
            if (ch == 'g') {
                octave--;
                std::cerr << "Octave: " << octave << " (C" << (4 + octave) << ")\n";
                continue;
            }
            if (ch == 'h') {
                octave++;
                std::cerr << "Octave: " << octave << " (C" << (4 + octave) << ")\n";
                continue;
            }
            if (ch == 'b') {
                duration = std::max(0.05f, duration * 0.5f);
                std::cerr << "Duration: " << duration << "s\n";
                continue;
            }
            if (ch == 'n') {
                duration = std::min(4.0f, duration * 2.0f);
                std::cerr << "Duration: " << duration << "s\n";
                continue;
            }
            if (ch == 'z') {
                currentInst = (currentInst + 1) % int(instruments.size());
                std::cerr << "Instrument: " << instruments[currentInst].name << "\n";
                continue;
            }

            // Note keys
            int semi = find_semitone(ch);
            if (semi < 0) continue;

            float noteNum = 60.0f + float(semi) + float(octave * 12);
            float freq = note_to_freq(noteNum);

            auto& li = instruments[currentInst];
            auto& vg = li.voices[li.nextVoice % li.voices.size()];
            li.nextVoice++;

            sync_write_pos();
            render_note(vg, noteNum, velocity, duration);

            // Note name for display
            static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            int nn = int(noteNum) % 12;
            int oct = int(noteNum) / 12 - 1;
            std::cerr << noteNames[nn] << oct << " (" << int(freq) << " Hz)\n";
        }

        shutdown_audio();
        std::cerr << "Bye!\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
