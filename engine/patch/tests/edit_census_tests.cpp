// The census: every measured patch (patches/library and patches/baselines)
// has every value, setting, string and position it holds set again through
// the edits. The edits must accept all of it, since the engine plays it, and
// the patch must come out equal to itself.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace mforce;

namespace {

struct Census {
  int patches{0}, nodes{0}, values{0}, settings{0}, strings{0}, positions{0}, canvas{0}, refusals{0};
};

// Runs one edit; a refusal is printed with where it happened and counted.
void again(Census& c, const std::string& file, const std::string& where, const std::function<void()>& edit) {
  try {
    edit();
  } catch (const std::exception& e) {
    ++c.refusals;
    std::printf("  REFUSED %s, %s: %s\n", file.c_str(), where.c_str(), e.what());
  }
}

void set_node_again(Census& c, Patch& p, const std::string& file, const Node& n) {
  ++c.nodes;
  for (const auto& [name, v] : n.values) {
    ++c.values;
    again(c, file, n.id + "." + name, [&, v = v] { set_value(p, n.id, name, v); });
  }
  for (const auto& [name, v] : n.settings) {
    ++c.settings;
    again(c, file, n.id + "." + name, [&, v = v] { set_setting(p, n.id, name, v); });
  }
  for (const auto& [name, text] : n.strings) {
    ++c.strings;
    again(c, file, n.id + "." + name, [&] { set_string(p, n.id, name, text); });
  }
}

void set_layout_again(Census& c, Patch& p, const std::string& file, const Layout& l) {
  for (const auto& [id, pos] : l.positions) {
    ++c.positions;
    again(c, file, "position of " + id, [&, pos = pos] { move_node(p, id, pos); });
  }
  if (l.outputPos) {
    ++c.canvas;
    again(c, file, "outputPos", [&] { move_output(p, *l.outputPos); });
  }
  if (l.panning) {
    ++c.canvas;
    again(c, file, "panning", [&] { set_panning(p, *l.panning); });
  }
}

void census_of(Census& c, const std::filesystem::path& path) {
  const std::string file = path.generic_string();
  const Patch original = load_patch(file);
  Patch p = original;
  ++c.patches;
  for (const Node& n : original.graph.nodes) set_node_again(c, p, file, n);
  set_layout_again(c, p, file, original.layout);
  if (!(p == original)) std::printf("  %s: not equal to itself after the census\n", file.c_str());
  CHECK(p == original);
}

}  // namespace

void run_edit_census_tests() {
  Census c;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
      if (entry.is_regular_file() && entry.path().extension() == ".json") census_of(c, entry.path());
  std::printf("  edit census: %d patches, %d nodes, %d values, %d settings, %d strings, %d positions, "
              "%d canvas points set again; %d refusals\n",
              c.patches, c.nodes, c.values, c.settings, c.strings, c.positions, c.canvas, c.refusals);
  CHECK(c.patches > 0);
  CHECK(c.refusals == 0);
}
