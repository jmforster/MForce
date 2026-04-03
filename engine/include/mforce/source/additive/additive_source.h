#pragma once
// AdditiveSource is now defined in full_additive_source.h (class FullAdditiveSource,
// type_name "AdditiveSource"). This header redirects for backward compatibility.
//
// For the old basic additive source, use basic_additive_source.h (BasicAdditiveSource).
#include "mforce/source/additive/full_additive_source.h"

namespace mforce {
// Alias so code using AdditiveSource gets the thin source (was FullAdditiveSource)
using AdditiveSource = FullAdditiveSource;
} // namespace mforce
