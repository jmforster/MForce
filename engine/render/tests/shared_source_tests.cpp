#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/pitched_instrument.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace mforce;


// A source wired to two pins is the same object on both pins: no wrapper, no
// advancer, no wiring order. A consumer the file lists before the advancer
// used to read a wrapper a sample late; it reads this sample's value now, so
// the shared patch renders exactly as the same patch with the envelope
// duplicated, however the file orders the consumers.
static std::string shared_env_patch_json(bool duplicated) {
    using nlohmann::json;
    const json table = {
        {"startVal", {0.0, 1.0, 0.7, 0.7}}, {"endVal", {1.0, 0.7, 0.7, 0.0}},
        {"power", {0.0, 0.0, 0.0, 0.0}}, {"type", {"Linear", "Linear", "Linear", "Linear"}},
        {"percent", {0.05, 0.1, 0.0, 0.25}}, {"minSec", {0.0, 0.0, 0.0, 0.0}},
        {"maxSec", {0.0, 0.0, 0.0, 0.0}}, {"nominal", {0.0, 0.0, 0.0, 0.0}}};
    auto env = [&](const char* id) {
        return json{{"id", id}, {"type", "Envelope"}, {"params", table}};
    };
    auto sine = [](const char* id, const char* envId) {
        return json{{"id", id}, {"type", "SineSource"},
                    {"params", {{"frequency", 220.0}, {"amplitude", {{"ref", envId}}}, {"phase", 0.0}}}};
    };
    json nodes = json::array();
    nodes.push_back(env("env"));
    if (duplicated) nodes.push_back(env("env2"));
    nodes.push_back(sine("sine2", "env"));                            // listed first: the file's advancer
    nodes.push_back(sine("sine1", duplicated ? "env2" : "env"));      // pulled first by the mix
    nodes.push_back(json{{"id", "mix"}, {"type", "CombinedSource"},
                         {"params", {{"operation", 3}, {"gainAdj", 0.0},
                                     {"source1", {{"ref", "sine1"}}}, {"source2", {{"ref", "sine2"}}}}}});
    const json root = {{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                       {"graph", {{"output", "mix"}, {"nodes", nodes}}}};
    return root.dump();
}

void run_shared_source_tests() {
    auto render_all = [](LoadedInstrument& p, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        p.instrument->render(b.data(), n);
        return b;
    };
    auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);
        a = std::max(0, a); z = std::min(z, int(b.size()));
        double s = 0.0;
        for (int i = a; i < z; ++i) s += double(b[size_t(i)]) * b[size_t(i)];
        return float(std::sqrt(s / double(std::max(1, z - a))));
    };
    auto ip = load_inst(shared_env_patch_json(false));
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr && !pi->pool().empty());
    auto& vg = pi->pool().voice(0);
    auto env = vg.nodes().at("env");
    CHECK(vg.nodes().at("sine2")->get_param("amplitude").get() == env.get());
    CHECK(vg.nodes().at("sine1")->get_param("amplitude").get() == env.get());   // the second consumer holds the node itself
    const int N = 24000;
    play_notes(*pi, {{.start = 0.0f, .duration = 0.25f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
    auto shared = render_all(ip, N);
    auto dupIp = load_inst(shared_env_patch_json(true));
    auto* dp = dynamic_cast<PitchedInstrument*>(dupIp.instrument.get());
    play_notes(*dp, {{.start = 0.0f, .duration = 0.25f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
    auto dup = render_all(dupIp, N);
    CHECK(shared.size() == dup.size() &&
          std::memcmp(shared.data(), dup.data(), shared.size() * sizeof(float)) == 0);
    CHECK(rms_of(shared, 0.05f, 0.20f) > 0.01f);                                   // and it sounds
}
