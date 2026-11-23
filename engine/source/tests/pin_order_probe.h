#pragma once
#include "mforce/core/dsp_value_source.h"
#include <string>
#include <vector>

using namespace mforce;


// The base drives a set's pins: each prepared once, started once and
// advanced once per next(), in declaration order; a composite forwards each
// verb to its sets and advances no pin of its own.
struct OrderProbe final : Generator {
    std::vector<std::string>* log{nullptr};
    std::string tag;
    float compute() override { if (log) log->push_back(tag); return 0.0f; }
    const char* type_name() const override { return "OrderProbe"; }
};
