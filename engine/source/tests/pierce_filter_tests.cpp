#include "mforce/testing/check.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/physical/pierce_filter.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <utility>
#include <vector>

using namespace mforce;

namespace {

struct Playback final : mforce::Generator {
    const std::vector<float>* buf{nullptr};
    size_t i{0};
    const char* type_name() const override { return "Playback"; }
    void on_start(int) override { i = 0; }
    float compute() override { return (buf && i < buf->size()) ? (*buf)[i++] : 0.0f; }
};

double worst_energy_ratio(const std::vector<float>& x, float aNeg, float aPos) {
    RenderContext ctx{48000};
    auto src = std::make_shared<Playback>();
    src->buf = &x;
    PierceFilterSource f;
    f.set_param("source", src);
    f.set_param("coefNeg", std::make_shared<ConstantSource>(aNeg));
    f.set_param("coefPos", std::make_shared<ConstantSource>(aPos));
    f.prepare(ctx); f.start(int(x.size()));
    double cx = 0.0, cy = 0.0, worst = 0.0;
    for (float s : x) {
        const double y = f.next();
        cx += double(s) * double(s);
        cy += y * y;
        if (cx > 1e-12) worst = std::fmax(worst, cy / cx);
    }
    return worst;
}
}  // namespace

void run_pierce_passivity_tests() {
    const int N = 48000;
    const double sr = 48000.0;
    std::vector<std::pair<const char*, std::vector<float>>> probes;
    std::mt19937 rng(7);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    std::vector<float> burst(N, 0.0f);
    for (int n = 0; n < N / 5; ++n) burst[n] = gauss(rng);
    probes.emplace_back("noiseburst", burst);

    std::vector<float> noise(N);
    for (int n = 0; n < N; ++n) noise[n] = gauss(rng);
    probes.emplace_back("noise", noise);

    std::vector<float> sweep(N);
    for (int n = 0; n < N; ++n) {
        const double t = n / sr;
        sweep[n] = float(std::sin(2.0 * 3.14159265358979 *
                                  (20.0 * t + (19980.0 / 2.0) * t * t)));
    }
    probes.emplace_back("sweep", sweep);

    std::vector<float> square(N);
    for (int n = 0; n < N; ++n)
        square[n] = std::sin(2.0 * 3.14159265358979 * 220.0 * n / sr) >= 0.0
                        ? 1.0f : -1.0f;
    probes.emplace_back("square_fullscale", square);

    std::vector<float> imp(N, 0.0f);
    imp[0] = 1.0f;
    probes.emplace_back("impulse", imp);
    probes.emplace_back("dc", std::vector<float>(N, 1.0f));

    const float pairs[][2] = {{0.5f, -0.5f},  {0.9f, -0.9f}, {0.99f, -0.99f},
                              {0.9f, 0.1f},   {-0.9f, 0.3f}, {0.0f, 0.8f},
                              {0.75f, -0.25f}, {0.999f, -0.999f}};
    double worst = 0.0;
    const char* worstName = "";
    for (const auto& p : probes)
        for (const auto& c : pairs) {
            const double r = worst_energy_ratio(p.second, c[0], c[1]);
            CHECK(r <= 1.0 + 1e-6);
            if (r > worst) { worst = r; worstName = p.first; }
        }
    std::printf("pierce passivity: worst cumulative out/in energy = %.9f "
                "(%s) over %d probes\n", worst, worstName,
                int(probes.size() * 8));

    // coefNeg == coefPos must be an exactly linear first-order allpass:
    // unity magnitude, so total output energy equals total input energy to
    // within the stored energy left in the state.
    const double lin = worst_energy_ratio(noise, 0.7f, 0.7f);
    CHECK(lin <= 1.0 + 1e-6);
    CHECK(lin > 0.999);
    std::printf("pierce linear check (coefNeg == coefPos == 0.7): "
                "energy ratio %.9f\n", lin);
}
