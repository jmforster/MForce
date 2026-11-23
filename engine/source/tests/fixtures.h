#pragma once
// Helpers the source tests share with the test programs of the modules above it.
#include "core/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/ramp.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/envelope/phased_value_source.h"
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


// A cycle with no tap, wired after the host calls (which walk the pins):
// illegal, and what the re-entry guards must keep finite.
inline std::shared_ptr<CombinedSource> tapless_cycle(TestHost& h) {
    auto loop = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                 std::make_shared<ConstantSource>(0.0f),
                                                 CombineOp::Sum, 0.0f);
    loop->prepare(h.ctx); loop->start(16);
    loop->set_param("source2", loop);
    return loop;
}
// A Phased node filled by name: its overlap, a feed per row and the four
// columns.
struct PhasedRow { std::shared_ptr<ValueSource> source; float percent, minSec, maxSec, gain; };
inline void fill_phased(PhasedValueSource& p, float overlap, std::initializer_list<PhasedRow> rows) {
    p.set_setting("overlap", overlap);
    std::vector<float> percent, minSec, maxSec, gain;
    for (const auto& r : rows) {
        p.add_param("sources", r.source);
        percent.push_back(r.percent); minSec.push_back(r.minSec);
        maxSec.push_back(r.maxSec);   gain.push_back(r.gain);
    }
    p.set_array("percent", percent); p.set_array("minSec", minSec);
    p.set_array("maxSec", maxSec);   p.set_array("gain", gain);
}

// The registered types that are parts, not signals: each implements its
// interface and nothing of ValueSource. The list grows as the families leave
// ValueSource and is the audit's oracle at the close.
static const std::set<std::string> kPartTypes = {
    "FormantPeaks", "FormantBands", "FormantSequence",
    "FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials", "ExpandRule",
    "PluckEvolution", "AveragingEvolution", "EKSEvolution", "ReedEvolution", "BowedStringEvolution",
    "BrassEvolution", "ReactionDiffusionEvolution", "SortErosionEvolution", "CellularAutomatonEvolution",
    "HistogramEqualizeEvolution", "BezierPullEvolution", "BitRotateEvolution", "TargetEvolution",
    "SoundChannel", "StereoMixer",
};

// The four layout factories, stage by stage: the form shapes the curved
// stages afterwards from its keys, so these are the factories' own values,
// which a direct caller (Vibrato's ramps) gets.
struct StageRow { float start, end; RampType type; float power, percent, minSec, maxSec; };
inline bool stages_are(const Envelope& e, std::initializer_list<StageRow> rows) {
    if (e.stage_count() != int(rows.size())) return false;
    int i = 0;
    for (const auto& r : rows) {
        const auto& s = e.stage(i++);
        if (s.ramp.startVal != r.start || s.ramp.endVal != r.end || s.ramp.type != r.type || s.ramp.power != r.power
            || s.percent != r.percent || s.minSec != r.minSec || s.maxSec != r.maxSec || s.nominal != 0.0f)
            return false;
    }
    return true;
}
namespace node_twins {
inline uint64_t fnv(const std::vector<float>& samples) {
    uint64_t h = 14695981039346656037ull;
    for (float v : samples) {
        const uint32_t b = std::bit_cast<uint32_t>(v);
        for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xFFu; h *= 1099511628211ull; }
    }
    return h;
}
// `notes` notes of `frames` samples each: prepare at 48 kHz, then start and pull.
inline uint64_t render(ValueSource& n, int frames, int notes = 1) {
    std::vector<float> out;
    out.reserve(size_t(frames) * size_t(notes));
    n.prepare(RenderContext{48000});
    for (int k = 0; k < notes; ++k) {
        n.start(frames);
        for (int i = 0; i < frames; ++i) out.push_back(n.next());
    }
    return fnv(out);
}
// The recorded answers, one per case: the hash each case rendered when the
// nodes took these values through their constructors.
static const std::pair<const char*, uint64_t> kBase[] = {
    {"bw lp s1 fixed", 0x7DA6617F2EF07EEDull},
    {"bw lp s1 moving", 0x1CF748556F117F90ull},
    {"bw lp s2 fixed", 0xE54E45AD12B6F94Eull},
    {"bw lp s2 moving", 0xD4ED49DF426328A5ull},
    {"bw lp s3 fixed", 0x9B04DD220CFCC334ull},
    {"bw lp s3 moving", 0x49BFF5C476E951FDull},
    {"bw lp s5 fixed", 0x1D495CEDFA615EF9ull},
    {"bw lp s5 moving", 0x8CA09047A4C97532ull},
    {"bw hp s1 fixed", 0x6C60F48C27D74030ull},
    {"bw hp s1 moving", 0x957B8416812577B5ull},
    {"bw hp s2 fixed", 0x878C7C87AFF7806Dull},
    {"bw hp s2 moving", 0x69D1F954EFB71716ull},
    {"bw hp s3 fixed", 0x0697BF2AA3F664CCull},
    {"bw hp s3 moving", 0x0F698B70757C41E7ull},
    {"bw hp s5 fixed", 0x7DFAD8E6FBB7717Aull},
    {"bw hp s5 moving", 0x6560F5D68C9F5863ull},
    {"bw bp s1 fixed", 0x2EB9D888CF17F7D3ull},
    {"bw bp s1 moving", 0x6CF772B4F98311B1ull},
    {"bw bp s2 fixed", 0xC63BA71D32C64700ull},
    {"bw bp s2 moving", 0x3AF89144A020CEBBull},
    {"bw bp s3 fixed", 0x74716CD94469318Bull},
    {"bw bp s3 moving", 0x2C3E846CA3BE0B40ull},
    {"bw bp s5 fixed", 0x554736ADFCD23C42ull},
    {"bw bp s5 moving", 0x58D552AC9CE75A09ull},
    {"hybrid t10", 0x65BF1FE0CB7E09FBull},
    {"hybrid t30", 0x59BB69A73FC03443ull},
    {"hybrid t40", 0x59BB69A73FC03443ull},
    {"hybrid t10 after prepare", 0x65BF1FE0CB7E09FBull},
    {"staticvar g0 seed0", 0xEB87129436C8DA65ull},
    {"staticrange g0 seed0", 0x0E9E2A5D8A488161ull},
    {"staticvar g0 seed1", 0xEB87129436C8DA65ull},
    {"staticrange g0 seed1", 0x68C88F41F56F6279ull},
    {"staticvar g1 seed0", 0xB5EAB293CFA4921Dull},
    {"staticrange g1 seed0", 0x7E0E21A9625BE551ull},
    {"staticvar g1 seed1", 0x4675EA76AD111F95ull},
    {"staticrange g1 seed1", 0xF9F8EB0EBC06BF9Dull},
    {"staticvar g2 seed0", 0xF0EF10E2BFF19241ull},
    {"staticrange g2 seed0", 0x47A55EC549607C71ull},
    {"staticvar g2 seed1", 0x21082F231FD6FDEDull},
    {"staticrange g2 seed1", 0xABF1E39866FD7BC1ull},
    {"staticvar g3 seed0", 0x3971B82E9C983CADull},
    {"staticrange g3 seed0", 0xD8D7B92104A117ADull},
    {"staticvar g3 seed1", 0xFEC36C0DEE946D69ull},
    {"staticrange g3 seed1", 0x855842CC60294771ull},
    {"vibrato g0 seed0", 0x16C06C18A210D1D1ull},
    {"vibrato g0 seed1", 0x4E91E896AB169CE9ull},
    {"vibrato g1 seed0", 0x0BC202720C42535Dull},
    {"vibrato g1 seed1", 0x23D18A0253408425ull},
    {"vibrato g2 seed0", 0xF10E1653FF9DF66Dull},
    {"vibrato g2 seed1", 0x5359D3A7ED07F895ull},
    {"vibrato g3 seed0", 0xA426CE46B48C4405ull},
    {"vibrato g3 seed1", 0x0EAD61D1DA83A411ull},
    {"vibrato g4 seed0", 0xB1B2AD3ADDF6DC49ull},
    {"vibrato g4 seed1", 0xE2AEAB8B0387E331ull},
    {"vibrato g5 seed0", 0x48A6C9FCB7685F25ull},
    {"vibrato g5 seed1", 0x48A6C9FCB7685F25ull},
    {"var absolute", 0xFA747A5C678A7F0Full},
    {"var relative", 0xDA52ED834A7DED6Bull},
    {"range normalized", 0x04F516117C2EF73Full},
    {"range raw", 0x1B0EBD249763BBD4ull},
    {"segment default seed", 0xF3965F2D63FBA2D5ull},
    {"segment seed 99", 0xB311CFFF6E6BF28Full},
    {"segment default seed oneShot", 0xECAD53185C918959ull},
    {"segment seed 99 oneShot", 0x6A67A1EF442F948Aull},
};
}  // namespace node_twins
