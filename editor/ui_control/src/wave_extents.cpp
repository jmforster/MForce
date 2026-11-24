// The least and greatest sample of each pixel column, served from a table of
// 64-sample buckets for the whole buckets inside a column.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "mforce/ui_control/waveforms.h"

namespace mforce::ui {

namespace {

constexpr std::size_t kBucket = std::size_t(ExtentCache::kBucket);

// `e` widened to hold `x`.
void widen(Extent& e, float x) {
  e.lo = std::min(e.lo, x);
  e.hi = std::max(e.hi, x);
}

ExtentCache::Table made(std::span<const float> samples) {
  ExtentCache::Table t;
  t.count = samples.size();
  t.buckets.assign((samples.size() + kBucket - 1) / kBucket, Extent{});
  for (std::size_t i = 0; i < samples.size(); ++i) {
    Extent& b = t.buckets[i / kBucket];
    if (i % kBucket == 0) b = Extent{samples[i], samples[i]};
    else widen(b, samples[i]);
    widen(t.all, samples[i]);              // starts at 0: the peaks are the greatest or 0, the least or 0
  }
  return t;
}

// The least and greatest of samples [from, to), from < to: the plain loop
// over the ends, the table over every whole bucket between them.
Extent column(std::span<const float> samples, const ExtentCache::Table& t, std::size_t from, std::size_t to) {
  Extent e{samples[from], samples[from]};
  std::size_t i = from;
  for (; i < to && i % kBucket != 0; ++i) widen(e, samples[i]);
  for (; i + kBucket <= to; i += kBucket) {
    widen(e, t.buckets[i / kBucket].lo);
    widen(e, t.buckets[i / kBucket].hi);
  }
  for (; i < to; ++i) widen(e, samples[i]);
  return e;
}

}  // namespace

const ExtentCache::Table& ExtentCache::table(int take, const std::string& node, std::span<const float> samples) {
  if (take != take_) {
    tables_.clear();
    take_ = take;
  }
  auto it = tables_.find(node);
  if (it == tables_.end() || it->second.count != samples.size()) it = tables_.insert_or_assign(node, made(samples)).first;
  return it->second;
}

int extents(const Strip& strip, int take, int zoom, int scroll, std::span<Extent> into, ExtentCache& cache) {
  const std::span<const float> samples = strip.samples;
  if (samples.empty() || zoom < 1) return 0;
  const ExtentCache::Table& t = cache.table(take, strip.node, samples);
  const std::uint64_t n = samples.size(), step = std::uint64_t(zoom);
  const std::uint64_t first = std::uint64_t(std::max(0, scroll));
  int filled = 0;
  for (std::size_t c = 0; c < into.size(); ++c) {
    const std::uint64_t from = first + c * step;
    if (from >= n) break;
    const std::uint64_t to = std::min(from + step, n);
    into[c] = column(samples, t, std::size_t(from), std::size_t(to));
    ++filled;
  }
  return filled;
}

Extent peaks(const Strip& strip, int take, ExtentCache& cache) {
  if (strip.samples.empty()) return Extent{};
  return cache.table(take, strip.node, strip.samples).all;
}

}  // namespace mforce::ui
