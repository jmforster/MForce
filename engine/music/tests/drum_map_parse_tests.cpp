// parse_drum_map: drum ids to patch paths, with its skips and its throw.
#include "mforce/testing/check.h"
#include "mforce/music/parse_util.h"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

using namespace mforce;

void run_drum_map_parse_tests() {
  std::printf("parse_drum_map: ids to paths, the entries it skips, an unknown id refused\n");
  const auto map = parse_drum_map("KK=patches/a.json;SN = patches/b.json ;03=c.json");
  CHECK(map.size() == 3);
  CHECK(map.count(0) && map.at(0) == "patches/a.json");
  CHECK(map.count(1) && map.at(1) == "patches/b.json");   // the id is the first two characters; the path trimmed
  CHECK(map.count(3) && map.at(3) == "c.json");
  CHECK(parse_drum_map("").empty());
  // No '=', an '=' before the third character, an empty path: each skipped.
  CHECK(parse_drum_map("KKpatches/a.json;K=b.json;HH=  ;;").empty());
  CHECK(parse_drum_map("KK=a.json;KK=b.json").at(0) == "b.json");   // the later entry wins
  bool threw = false;
  try {
    (void)parse_drum_map("ZZ=z.json");
  } catch (const std::runtime_error& e) {
    threw = std::string(e.what()) == "Unknown drum ID: ZZ";
  }
  CHECK(threw);
}
