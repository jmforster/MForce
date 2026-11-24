#include "mforce/ui_model/history.h"
#include <cstddef>
#include <utility>

namespace mforce::ui {

namespace {

constexpr std::size_t kMostSteps = 200;

}  // namespace

void remember(History& h, const Patch& before) {
  if (h.savedAt > int(h.past.size())) h.savedAt = -1;   // the saved state was in the future that goes
  h.future.clear();
  h.past.push_back(before);
  if (h.past.size() <= kMostSteps) return;
  h.past.erase(h.past.begin());
  if (h.savedAt >= 0) --h.savedAt;                      // 0 went with the oldest: the saved state is gone
}

bool undo(History& h, Patch& patch) {
  if (h.past.empty()) return false;
  h.future.push_back(std::move(patch));
  patch = std::move(h.past.back());
  h.past.pop_back();
  return true;
}

bool redo(History& h, Patch& patch) {
  if (h.future.empty()) return false;
  h.past.push_back(std::move(patch));
  patch = std::move(h.future.back());
  h.future.pop_back();
  return true;
}

void mark_saved(History& h) { h.savedAt = int(h.past.size()); }

bool unsaved(const History& h) { return h.savedAt != int(h.past.size()); }

}  // namespace mforce::ui
