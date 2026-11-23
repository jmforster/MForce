#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/compose/passage_melody.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/music/basics.h"
#include "mforce/music/music_json.h"
#include <nlohmann/json.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mforce;


void run_passage_melody_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    // House convention: E5 = 64, D5 = 62, C5 = 60, G5 = 67.
    CHECK(scale_steps_between(64.0f, 62.0f, c) == -1);   // E -> D
    CHECK(scale_steps_between(62.0f, 60.0f, c) == -1);   // D -> C
    CHECK(scale_steps_between(64.0f, 67.0f, c) ==  2);   // E -> G (E F G)
    CHECK(scale_steps_between(64.0f, 64.0f, c) ==  0);
    CHECK(scale_steps_between(60.0f, 72.0f, c) ==  7);   // C5 -> C6, full octave
    CHECK(scale_steps_between(72.0f, 59.0f, c) == -8);   // C6 -> B4, crosses octave
    // Non-scale tone throws (crawl material must be diatonic).
    bool threw = false;
    try { scale_steps_between(60.0f, 61.0f, c); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // Non-C tonic: G Major, F#5 = 66 is a scale tone, F5 = 65 is not.
    Scale g = Scale::get("G", "Major");
    CHECK(scale_steps_between(67.0f, 66.0f, g) == -1);   // G -> F#
    threw = false;
    try { scale_grid_index(65.0f, g); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    const std::string mary =
        "Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | "
        "Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw";
    auto phrases = phrases_from_passage(mary, 5, c, 4.0f);
    CHECK(phrases.size() == 2);

    // Phrase 1: 4 figures, DENSE connectors (one per figure, [0] a dummy —
    // realize_events reads connectors[i] as the lead INTO figure i),
    // starts E5.
    const auto& p1 = phrases[0];
    CHECK(p1.figures.size() == 4);
    CHECK(p1.connectors.size() == 4);
    CHECK(p1.startingPitch && int(p1.startingPitch->note_number()) == 64);
    CHECK(p1.cadenceType == 0);
    // Every figure Locked with a lockedFigure present.
    for (const auto& ft : p1.figures) {
        CHECK(ft.source == FigureSource::Locked);
        CHECK(ft.lockedFigure.has_value());
    }
    // fig1 = E D C D: durations 1,1,1,1; steps 0,-1,-1,+1.
    {
        const auto& u = p1.figures[0].lockedFigure->units;
        CHECK(u.size() == 4);
        CHECK(u[0].duration == 1.0f && u[0].step == 0);
        CHECK(u[1].step == -1 && u[2].step == -1 && u[3].step == 1);
    }
    // fig4 = E G Gh: durations 1,1,2; steps 0,+2,0.
    {
        const auto& u = p1.figures[3].lockedFigure->units;
        CHECK(u.size() == 3);
        CHECK(u[2].duration == 2.0f);
        CHECK(u[0].step == 0 && u[1].step == 2 && u[2].step == 0);
    }
    // Connectors: [0] dummy 0, then +1 (D->E), -1 (E->D), +1 (D->E).
    CHECK(p1.connectors[0] && p1.connectors[0]->leadStep == 0);
    CHECK(p1.connectors[1] && p1.connectors[1]->leadStep == 1);
    CHECK(p1.connectors[2] && p1.connectors[2]->leadStep == -1);
    CHECK(p1.connectors[3] && p1.connectors[3]->leadStep == 1);

    // Phrase 2: last figure is the whole-note C alone in its bar.
    const auto& p2 = phrases[1];
    CHECK(p2.figures.size() == 4);
    CHECK(p2.connectors.size() == 4);
    CHECK(p2.startingPitch && int(p2.startingPitch->note_number()) == 64);
    {
        const auto& u = p2.figures[3].lockedFigure->units;
        CHECK(u.size() == 1 && u[0].duration == 4.0f && u[0].step == 0);
    }
    // fig3 = D D E D: steps 0,0,+1,-1; its lead connector is -1 (E->D).
    CHECK(p2.figures[2].lockedFigure->units[2].step == 1);
    CHECK(p2.connectors[2] && p2.connectors[2]->leadStep == -1);
    // fig4's lead is -1 (D->C).
    CHECK(p2.connectors[3] && p2.connectors[3]->leadStep == -1);

    // A '|'-free string is one phrase (parse marks every note phraseStart
    // only when grouping is active — verify we don't fragment).
    auto one = phrases_from_passage("Cq Dq Eq Fq", 5, c, 4.0f);
    CHECK(one.size() == 1 && one[0].figures.size() == 1);

    // Rests refuse loudly (parse's rest-ends-phrase rule would corrupt
    // structural grouping; revisit when a crawl tune needs rests).
    threw = false;
    try { phrases_from_passage("Cq Rq Eq", 5, c, 4.0f); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Schema + apply: template with a melodyPassageFile gets its phrases
    // derived; melodyPassageFile and melodyOctave round-trip; a missing file throws.
    {
        // Write a scratch .psg next to the test's CWD-independent temp dir.
        const std::string psgPath = "renders/scratch/_pm_test.psg";
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(psgPath); f << "Eq Dq Cq Dq | Cq Cq Ch"; }

        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80,
          "sections": [{"name": "Main", "beats": 8}],
          "parts": [{
            "name": "melody", "role": "melody",
            "passages": { "Main": {
              "melodyPassageFile": "renders/scratch/_pm_test.psg",
              "melodyOctave": 5, "phrases": [] } }
          }]
        })");
        PieceTemplate tmpl = template_from(tj);
        CHECK(tmpl.parts[0].passages.at("Main").melodyPassageFile
              == "renders/scratch/_pm_test.psg");
        CHECK(tmpl.parts[0].passages.at("Main").melodyOctave == 5);

        apply_passage_melodies(tmpl);
        const auto& pass = tmpl.parts[0].passages.at("Main");
        CHECK(pass.phrases.size() == 2);
        CHECK(pass.phrases[0].figures.size() == 1);   // one bar
        CHECK(pass.phrases[1].figures.size() == 1);

        // Round-trip keeps the fields.
        const json out = json_of(tmpl)["parts"][0]["passages"]["Main"];
        CHECK(out.value("melodyPassageFile", std::string())
              == "renders/scratch/_pm_test.psg");
        CHECK(out.value("melodyOctave", 0) == 5);

        // Missing file names the path.
        tmpl.parts[0].passages.at("Main").melodyPassageFile = "no/such.psg";
        threw = false;
        try { apply_passage_melodies(tmpl); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("no/such.psg") != std::string::npos;
        }
        CHECK(threw);
        std::filesystem::remove(psgPath);
    }
}
