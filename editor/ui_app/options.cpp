#include "options.h"
#include <stdexcept>

namespace mforce::ui {

namespace {

const char* const kUsage =
    "usage: mforce_editor [patch.json] [--frames N] [--shot FILE] [--select NAME] [--tab note|passage|chords|drums]";

int frame_count(const std::string& text) {
  try {
    const int n = std::stoi(text);
    if (n >= 0) return n;
  } catch (const std::exception&) {                 // not a number, or out of range
  }
  throw std::runtime_error(std::string(kUsage) + ": N is a count of frames, 0 or more");
}

}  // namespace

Options read_options(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--frames" && hasValue) o.frames = frame_count(argv[++i]);
    else if (arg == "--shot" && hasValue) o.shot = argv[++i];
    else if (arg == "--select" && hasValue) o.select = argv[++i];
    else if (arg == "--tab" && hasValue) o.tab = argv[++i];
    else if (!arg.empty() && arg[0] != '-' && o.patch.empty()) o.patch = arg;
    else throw std::runtime_error(kUsage);
  }
  if (!o.shot.empty() && o.frames < 1) throw std::runtime_error(std::string(kUsage) + ": --shot needs --frames");
  if (!o.tab.empty() && o.tab != "note" && o.tab != "passage" && o.tab != "chords" && o.tab != "drums")
    throw std::runtime_error(std::string(kUsage) + ": --tab names note, passage, chords or drums");
  return o;
}

Transport with_tab(Transport t, const std::string& tab) {
  if (tab == "passage") t.mode = Transport::Mode::Passage;
  else if (tab == "chords") t.mode = Transport::Mode::Chords;
  else if (tab == "drums") t.mode = Transport::Mode::Drums;
  else t.mode = Transport::Mode::Note;
  return t;
}

}  // namespace mforce::ui
