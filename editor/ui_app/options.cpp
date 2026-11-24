#include "options.h"
#include <stdexcept>
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/pane.h"
#include "mforce/ui_control/waveforms.h"

namespace mforce::ui {

namespace {

const char* const kUsage =
    "usage: mforce_editor [patch.json] [--frames N] [--shot FILE] [--select NAME] [--tab note|passage|chords|drums]"
    " [--generate] [--pane waveforms|spectrum|keyboard] [--columns N] [--popout NAME] [--collapse] [--listen NAME]";

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
    else if (arg == "--generate") o.generate = true;
    else if (arg == "--pane" && hasValue) o.pane = argv[++i];
    else if (arg == "--columns" && hasValue) o.columns = frame_count(argv[++i]);
    else if (arg == "--popout" && hasValue) o.popout = argv[++i];
    else if (arg == "--collapse") o.collapse = true;
    else if (arg == "--listen" && hasValue) o.listen = argv[++i];
    else if (!arg.empty() && arg[0] != '-' && o.patch.empty()) o.patch = arg;
    else throw std::runtime_error(kUsage);
  }
  if (!o.shot.empty() && o.frames < 1) throw std::runtime_error(std::string(kUsage) + ": --shot needs --frames");
  if (!o.tab.empty() && o.tab != "note" && o.tab != "passage" && o.tab != "chords" && o.tab != "drums")
    throw std::runtime_error(std::string(kUsage) + ": --tab names note, passage, chords or drums");
  return o;
}

void apply_picture_options(Session& s, const Options& o) {
  if (!o.listen.empty()) listen_at(s, o.listen);
  if (o.generate) generate(s);
  if (o.pane == "spectrum") show_pane_tab(s, Pane::Tab::Spectrum);
  else if (o.pane == "keyboard") show_pane_tab(s, Pane::Tab::Keyboard);
  if (o.columns > 0) set_columns(s.waves, o.columns);
  if (!o.popout.empty()) open_pop_out(s, o.popout == "Output" ? std::string() : o.popout);
  if (o.collapse) collapse_pane(s, true);
}

Transport with_tab(Transport t, const std::string& tab) {
  if (tab == "passage") t.mode = Transport::Mode::Passage;
  else if (tab == "chords") t.mode = Transport::Mode::Chords;
  else if (tab == "drums") t.mode = Transport::Mode::Drums;
  else t.mode = Transport::Mode::Note;
  return t;
}

}  // namespace mforce::ui
