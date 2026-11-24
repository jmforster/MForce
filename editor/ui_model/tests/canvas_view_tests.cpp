// canvas_panning: the saved panning, or one that brings the top-left corner
// of what the canvas draws to the canvas's (fit_panning); and the counts.
#include "mforce/testing/check.h"
#include "mforce/ui_model/canvas_view.h"

using namespace mforce;
using namespace mforce::ui;

void run_canvas_view_tests() {
  Patch p;
  CHECK(canvas_panning(p) == (Vec2{40, 40}));             // nothing to show: the margin
  p.layout.positions = {{"a", Vec2{-560, 0}}, {"b", Vec2{-280, 120}}};
  p.layout.outputPos = Vec2{280, -30};
  CHECK(canvas_panning(p) == (Vec2{600, 70}));            // the least x and y, a margin in
  p.layout.panning = Vec2{5, 6};
  CHECK(canvas_panning(p) == (Vec2{5, 6}));               // a saved panning wins
  CHECK(fit_panning(p) == (Vec2{600, 70}));               // whatever is saved

  // A face's position counts; its node's does not.
  Patch faced;
  faced.layout.positions = {{"a", Vec2{0, 0}}, {"pf", Vec2{-900, -900}}};
  faced.layout.faces = {NoteFace{"Note1", {{"frequency", "pf"}}, Vec2{-100, 10}}};
  CHECK(fit_panning(faced) == (Vec2{140, 40}));
}

void run_canvas_count_tests() {
  Patch p;
  CHECK(canvas_counts(p).nodes == 0 && canvas_counts(p).wires == 0);
  Node pf, osc, env, l;
  pf.id = "pf";
  pf.type = "PerformNode";
  env.id = "env";
  env.type = "Envelope";
  env.dynamicPins["timeScale"] = "pf";
  osc.id = "osc";
  osc.type = "SineSource";
  osc.wires["frequency"] = {Wire{"pf"}};
  osc.wires["amplitude"] = {Wire{"env"}};
  l.id = "l";
  l.type = "MultiSource";
  l.wires["source"] = {Wire{"osc"}, Wire{"env", true}};
  p.graph.nodes = {pf, env, osc, l};
  p.layout.faces = {NoteFace{"Note1", {{"frequency", "pf"}}, std::nullopt}, NoteFace{"Note2", {}, std::nullopt}};
  CHECK(canvas_counts(p).nodes == 5);                    // env, osc, l and the two faces; pf is a face's
  CHECK(canvas_counts(p).wires == 5);                    // four feeds and a dynamic pin
  p.graph.output = "l";
  CHECK(canvas_counts(p).wires == 6);                    // and the output
}
