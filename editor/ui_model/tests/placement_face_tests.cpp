// place_unplaced_nodes with note faces, and with positions no canvas can draw.
#include "mforce/testing/check.h"
#include "mforce/ui_model/placement.h"
#include "placement_fixture.h"
#include <limits>

using namespace mforce;
using namespace mforce::ui;
using namespace placement_fixture;

// A face is one thing to place, in its nodes' nearest column with six rows;
// its node gets no position; a face that has a position keeps it.
void run_placement_face_tests() {
  Patch p = small();
  p.layout.faces = {NoteFace{"Note1", {{"frequency", "pf"}}, std::nullopt}, NoteFace{"Kept", {}, Vec2{-5000, 0}}};
  CHECK(place_unplaced_nodes(p) == 6);                    // four nodes, a face and the Output node
  CHECK(!p.layout.positions.count("pf"));
  REQUIRE(p.layout.faces[0].pos.has_value());
  CHECK(p.layout.faces[0].pos->x == at(p, "env").x);      // column 1, read by osc as pf was
  CHECK(at(p, "env").y - p.layout.faces[0].pos->y == slot_of(nullptr));   // at pf's place, six rows tall
  CHECK(*p.layout.faces[1].pos == (Vec2{-5000, 0}));
  CHECK(all_distinct(p));
}

// A position that is not finite, or with |x| or |y| of 1e7 or more, is no
// position: the thing is placed afresh, as though it had none.
void run_placement_fault_tests() {
  const float inf = std::numeric_limits<float>::infinity();
  Patch clean = small();
  clean.layout.faces = {NoteFace{"Note1", {{"frequency", "pf"}}, std::nullopt}};
  (void)place_unplaced_nodes(clean);
  Patch faulty = small();
  faulty.layout.faces = {NoteFace{"Note1", {{"frequency", "pf"}}, Vec2{0, 2.4e13f}}};
  faulty.layout.positions = {{"osc", Vec2{1.19e27f, 0}}, {"env", Vec2{inf, 0}}, {"lone", Vec2{0, -1.0e7f}}};
  faulty.layout.outputPos = Vec2{-inf, 3};
  (void)place_unplaced_nodes(faulty);
  CHECK(faulty.layout == clean.layout);
  Patch near = small();
  near.layout.positions = {{"osc", Vec2{9.9e6f, -9.9e6f}}};
  (void)place_unplaced_nodes(near);
  CHECK(near.layout.positions.at("osc") == (Vec2{9.9e6f, -9.9e6f}));   // inside the bound: kept
}
