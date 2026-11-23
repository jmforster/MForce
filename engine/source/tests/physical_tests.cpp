#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/physical/allpass_resonator.h"
#include "mforce/source/physical/bow_table_source.h"
#include "mforce/source/physical/delay_line_source.h"
#include "mforce/source/physical/ks_string.h"
#include "mforce/source/physical/mesh2d_source.h"
#include "mforce/source/physical/pierce_filter.h"
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;


// Every type prepares and renders finite values, declares its pins in the
// order they are advanced, and keeps its defaults.
void run_physical_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    AllpassResonator ar;          smoke(ar);
    BowTableSource bt;            smoke(bt);
    DelayLineSource dl;           smoke(dl);
    KSString ks;                  smoke(ks);
    Mesh2DSource mesh;            smoke(mesh);
    PierceFilterSource pierce;    smoke(pierce);

    CHECK(pin_order(ar) == "source,frequency,amplitude,");
    CHECK(pin_order(bt) == "source,slope,offset,");
    CHECK(pin_order(dl) == "source,frequency,ratio,amplitude,");
    CHECK(pin_order(ks) == "source,frequency,amplitude,damper,bow,");
    CHECK(pin_order(mesh) ==
          "source,inX,inY,outX,outY,decay,edgeFc,edgeR,coefNeg,coefPos,");
    CHECK(pin_order(pierce) == "source,coefNeg,coefPos,");

    CHECK(input_names(ar) == "source," && input_names(bt) == "source,");
    CHECK(input_names(ks) == "source,damper,bow,");
    CHECK(param_names(ks) == "frequency,amplitude,");
    CHECK(param_names(pierce) == "coefNeg,coefPos,");
    CHECK(setting_names(ar) == "feedback,damping,stiffness,tension,exciteGain,direct,");
    CHECK(setting_names(bt) == "minOutput,maxOutput,");
    CHECK(setting_names(dl) == "compensate,");
    CHECK(setting_names(mesh) == "cols,rows,edgeMode,");
    CHECK(setting_names(pierce).empty());
    CHECK(setting_names(ks) ==
          "numCombs,detune,spreadJitter,excStagger,t60,brightness,dispersion,"
          "inharmGain,inharmFb,inharmHp,ap1,ap2,ap3,fbCoeff,releaseFb,damperNoise,"
          "exciteGain,direct,bowSpeed,frictionGain,bowGain,");

    CHECK(!ks.tracks_frequency_live());
    CHECK(dl.tracks_frequency_live() && mesh.tracks_frequency_live());

    // a declared default wins over anything the constructor once set
    CHECK_NEAR(ks.get_setting("detune"), 0.75f, 1e-6f);
    CHECK_NEAR(ks.get_setting("numCombs"), 3.0f, 1e-6f);
    CHECK_NEAR(param_default(ks, "frequency"), 220.0f, 1e-6f);
    CHECK_NEAR(param_default(mesh, "edgeFc"), 1575.0f, 1e-6f);
    CHECK_NEAR(param_default(pierce, "coefPos"), -0.5f, 1e-6f);
    CHECK_NEAR(mesh.get_setting("cols"), 12.0f, 1e-6f);
    CHECK_NEAR(dl.get_setting("compensate"), 0.0f, 1e-6f);
    for (const auto& d : mesh.setting_descriptors())
        if (std::string(d.name) == "edgeMode") {
            CHECK(d.enum_labels != nullptr && std::string(d.enum_labels[2]) == "pierce");
            CHECK(d.enum_labels[3] == nullptr);
        }
}

// The base prepares and advances every wired pin once per note and once per
// sample, so a node reads each pin's current value in compute().
void run_physical_auto_pin_tests() {
    RenderContext ctx{48000};
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 7; ++i) CHECK(std::isfinite(s.next()));
        for (const auto& p : probes) CHECK(p->starts == 1 && p->nexts == 7);
    };
    AllpassResonator ar;
    count(ar, {"source", "frequency", "amplitude"});
    BowTableSource bt;
    count(bt, {"source", "slope", "offset"});
    DelayLineSource dl;
    count(dl, {"source", "frequency", "ratio", "amplitude"});
    KSString ks;
    count(ks, {"source", "frequency", "amplitude", "damper", "bow"});
    Mesh2DSource mesh;
    count(mesh, {"source", "inX", "inY", "outX", "outY", "decay", "edgeFc", "edgeR",
                 "coefNeg", "coefPos"});
    PierceFilterSource pierce;
    count(pierce, {"source", "coefNeg", "coefPos"});

    // an unwired pin reads its declared constant and is not a source
    CHECK(ks.get_param("damper") != nullptr && ks.get_param("bow") != nullptr);
    KSString bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("frequency") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The base advances the pins in declaration order, the inherited `source`
// first.
void run_physical_advance_order_tests() {
    RenderContext ctx{48000};
    auto order = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<int> log;
        std::vector<std::shared_ptr<OrderProbe>> probes;
        std::vector<int> expect;
        int id = 0;
        for (const char* n : names) {
            probes.push_back(std::make_shared<OrderProbe>(&log, ++id));
            s.set_param(n, probes.back());
            expect.push_back(id);
        }
        s.prepare(ctx); s.start(4800);
        s.next();
        CHECK(log == expect);
    };
    AllpassResonator ar;
    order(ar, {"source", "frequency", "amplitude"});
    BowTableSource bt;
    order(bt, {"source", "slope", "offset"});
    DelayLineSource dl;
    order(dl, {"source", "frequency", "ratio", "amplitude"});
    KSString ks;
    order(ks, {"source", "frequency", "amplitude", "damper", "bow"});
    Mesh2DSource mesh;
    order(mesh, {"source", "inX", "inY", "outX", "outY", "decay", "edgeFc", "edgeR",
                 "coefNeg", "coefPos"});
    PierceFilterSource pierce;
    order(pierce, {"source", "coefNeg", "coefPos"});
}

// Settings the old set_setting clamped are still clamped; the others are not.
void run_physical_setting_hook_tests() {
    KSString ks;
    ks.set_setting("numCombs", 40.0f);   CHECK_NEAR(ks.get_setting("numCombs"), 10.0f, 1e-6f);
    ks.set_setting("numCombs", 0.0f);    CHECK_NEAR(ks.get_setting("numCombs"), 1.0f, 1e-6f);
    ks.set_setting("t60", 0.001f);       CHECK_NEAR(ks.get_setting("t60"), 0.05f, 1e-6f);
    ks.set_setting("t60", 600.0f);       CHECK_NEAR(ks.get_setting("t60"), 600.0f, 1e-6f);
    ks.set_setting("ap1", 5.0f);         CHECK_NEAR(ks.get_setting("ap1"), 0.95f, 1e-6f);
    ks.set_setting("ap3", -5.0f);        CHECK_NEAR(ks.get_setting("ap3"), -0.95f, 1e-6f);
    ks.set_setting("brightness", 9.0f);  CHECK_NEAR(ks.get_setting("brightness"), 1.0f, 1e-6f);
    ks.set_setting("releaseFb", -2.0f);  CHECK_NEAR(ks.get_setting("releaseFb"), 0.0f, 1e-6f);
    ks.set_setting("damperNoise", 9.0f); CHECK_NEAR(ks.get_setting("damperNoise"), 2.0f, 1e-6f);
    ks.set_setting("excStagger", 1.0f);  CHECK_NEAR(ks.get_setting("excStagger"), 0.02f, 1e-6f);
    ks.set_setting("detune", 99999.0f);  CHECK_NEAR(ks.get_setting("detune"), 99999.0f, 1e-3f);
    ks.set_setting("inharmGain", -3.0f); CHECK_NEAR(ks.get_setting("inharmGain"), -3.0f, 1e-6f);
    ks.set_setting("nosuch", 1.0f);      CHECK_NEAR(ks.get_setting("nosuch"), 0.0f, 1e-6f);

    AllpassResonator ar;
    ar.set_setting("feedback", 5.0f);    CHECK_NEAR(ar.get_setting("feedback"), 0.9999f, 1e-6f);
    ar.set_setting("damping", -1.0f);    CHECK_NEAR(ar.get_setting("damping"), 0.05f, 1e-6f);
    ar.set_setting("stiffness", 9.0f);   CHECK_NEAR(ar.get_setting("stiffness"), 0.95f, 1e-6f);
    ar.set_setting("tension", -2.0f);    CHECK_NEAR(ar.get_setting("tension"), 0.0f, 1e-6f);
    ar.set_setting("direct", 7.0f);      CHECK_NEAR(ar.get_setting("direct"), 7.0f, 1e-6f);

    DelayLineSource dl;
    dl.set_setting("compensate", 0.4f);  CHECK_NEAR(dl.get_setting("compensate"), 1.0f, 1e-6f);
    dl.set_setting("compensate", 0.0f);  CHECK_NEAR(dl.get_setting("compensate"), 0.0f, 1e-6f);

    BowTableSource bt;
    bt.set_setting("minOutput", 0.2f);   CHECK_NEAR(bt.get_setting("minOutput"), 0.2f, 1e-6f);
    bt.set_setting("maxOutput", 7.0f);   CHECK_NEAR(bt.get_setting("maxOutput"), 7.0f, 1e-6f);
}

// The mesh size and the edge mode are clamped, and each change clears the
// mesh: a ringing mesh with no excitation falls silent at once.
void run_physical_mesh_hook_tests() {
    Mesh2DSource mesh;
    mesh.set_setting("cols", 500.0f);    CHECK_NEAR(mesh.get_setting("cols"), 64.0f, 1e-6f);
    mesh.set_setting("rows", 1.0f);      CHECK_NEAR(mesh.get_setting("rows"), 2.0f, 1e-6f);
    mesh.set_setting("edgeMode", 9.0f);  CHECK_NEAR(mesh.get_setting("edgeMode"), 2.0f, 1e-6f);
    mesh.set_setting("edgeMode", -3.0f); CHECK_NEAR(mesh.get_setting("edgeMode"), 0.0f, 1e-6f);

    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(0.0f);
    Mesh2DSource ring;
    ring.set_param("source", in);
    ring.set_setting("cols", 8.0f);
    ring.set_setting("rows", 6.0f);
    ring.prepare(ctx); ring.start(4800);
    in->set(1.0f); ring.next(); in->set(0.0f);
    float energy = 0.0f;
    for (int i = 0; i < 60; ++i) energy += std::fabs(ring.next());
    CHECK(energy > 0.0f);
    ring.set_setting("edgeMode", 1.0f);
    for (int i = 0; i < 20; ++i) CHECK(ring.next() == 0.0f);
}

// The pins are read in compute(): the bow table's curve, KSString's bow mode.
void run_physical_behavior_tests() {
    RenderContext ctx{48000};
    BowTableSource bt;
    bt.set_param("source", std::make_shared<ConstantSource>(1.0f));
    bt.prepare(ctx); bt.start(4800);
    CHECK_NEAR(bt.next(), 0.01f, 1e-6f);           // rc clamped up to minOutput
    bt.set_param("slope", std::make_shared<ConstantSource>(0.5f));
    CHECK_NEAR(bt.next(), std::pow(1.25f, -4.0f), 1e-5f);
    bt.set_param("offset", std::make_shared<ConstantSource>(1.0f));
    CHECK_NEAR(bt.next(), std::pow(1.75f, -4.0f), 1e-5f);

    // the bow pin switches the bow junction on only while it is wired
    auto strike = [&](bool bowed) {
        auto in = std::make_shared<ConstantSource>(0.0f);
        KSString ks;
        ks.set_param("source", in);
        if (bowed) ks.set_param("bow", std::make_shared<ConstantSource>(0.8f));
        ks.prepare(ctx); ks.start(4800);
        in->set(1.0f); ks.next(); in->set(0.0f);
        float sum = 0.0f;
        for (int i = 0; i < 2000; ++i) sum += ks.next();
        return sum;
    };
    const float plucked = strike(false), bowed = strike(true);
    CHECK(std::isfinite(plucked) && std::isfinite(bowed));
    CHECK(plucked != bowed);
}
