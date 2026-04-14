#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace mforce {

// ===========================================================================
// DUN format parser — compact scale-degree melody notation
//
// Header:  key:D scale:Major meter:4/4 bpm:108
// Tokens:  qn qu1 qd2 h.n en eu3 ...
// Markers: | = figure boundary, / = phrase boundary
// Comments: lines starting with #
// ===========================================================================

struct DunHeader {
  std::string keyName{"C"};
  std::string scaleName{"Major"};
  int meterNum{4};
  int meterDen{4};
  float bpm{120.0f};
  int startDegree{0};     // 0-based scale degree (0=root, 2=3rd, 4=5th)
  int startOctave{-1};    // -1 = use default (4)
  float pickupBeats{0};   // beats of pickup before first downbeat (0 = none)
};

struct DunToken {
  float duration;      // in beats
  int step;            // movement in scale degrees (0 = none, +N = up, -N = down)
  bool rest{false};    // true = silence for this duration
  int accidental{0};   // +1 = sharp, -1 = flat (transient, doesn't affect cursor)
  bool tied{false};    // true = this token is a tie, its duration should be merged into the previous token
  Ornament ornament{};  // monostate (None) by default
};

struct DunParseResult {
  DunHeader header;
  // Phrases, each containing figures, each containing tokens
  std::vector<std::vector<std::vector<DunToken>>> phrases;
};

// ---------------------------------------------------------------------------
// Parse a single token like "qn", "q.u2", "ed1", "h.n", "sn"
// ---------------------------------------------------------------------------
inline DunToken parse_token(const std::string& tok) {
  if (tok.empty()) throw std::runtime_error("Empty DUN token");

  int pos = 0;

  // Duration base
  float dur;
  switch (tok[pos++]) {
    case 's': dur = 0.25f; break;
    case 'e': dur = 0.5f;  break;
    case 'q': dur = 1.0f;  break;
    case 'h': dur = 2.0f;  break;
    case 'w': dur = 4.0f;  break;
    default:
      throw std::runtime_error("Bad duration in DUN token: " + tok);
  }

  // Dotted?
  if (pos < int(tok.size()) && tok[pos] == '.') {
    dur *= 1.5f;
    pos++;
  }

  // Direction + magnitude, or rest
  // u/d = up/down by scale degrees, n = no movement, r = rest
  // Optional +/- suffix = transient accidental (sharp/flat)
  if (pos >= int(tok.size()))
    throw std::runtime_error("Missing direction in DUN token: " + tok);

  int step = 0;
  bool isRest = false;
  int accidental = 0;
  bool tied = false;
  char dir = tok[pos++];
  switch (dir) {
    case 'r':
      isRest = true;
      break;
    case 'N': case 'n':
      step = 0;
      break;
    case 'T': case 't':
      // Tie to previous note: duration will be merged by the parser
      tied = true;
      step = 0;
      break;
    case 'U': case 'u':
    case 'D': case 'd': {
      bool up = (dir == 'U' || dir == 'u');
      int mag = 0;
      while (pos < int(tok.size()) && tok[pos] >= '0' && tok[pos] <= '9') {
        mag = mag * 10 + (tok[pos] - '0');
        pos++;
      }
      if (mag == 0) mag = 1;
      step = up ? mag : -mag;
      break;
    }
    default:
      throw std::runtime_error("Bad direction '" + std::string(1, dir) +
                               "' in DUN token: " + tok);
  }

  // Check for accidental suffix: + (sharp) or - (flat)
  if (pos < int(tok.size())) {
    if (tok[pos] == '+') { accidental = 1; pos++; }
    else if (tok[pos] == '-') { accidental = -1; pos++; }
  }

  // Check for ornament suffix (two chars, or ~ for trill):
  //   ~ or tr = trill (above)
  //   mu = mordent above,  md = mordent below
  //   tu = turn above,     td = turn below
  Ornament ornament{};
  if (pos < int(tok.size())) {
    if (tok[pos] == '~') {
      ornament = Trill{1, 2, {}};
      pos++;
    } else if (pos + 1 < int(tok.size())) {
      char a = tok[pos], b = tok[pos + 1];
      if (a == 't' && b == 'r') { ornament = Trill{1, 2, {}}; pos += 2; }
      else if (a == 'm' && b == 'u') { ornament = Mordent{1, 2, {}}; pos += 2; }
      else if (a == 'm' && b == 'd') { ornament = Mordent{-1, 2, {}}; pos += 2; }
      else if (a == 't' && b == 'u') { ornament = Turn{1, 2, 2, {}}; pos += 2; }
      else if (a == 't' && b == 'd') { ornament = Turn{-1, 2, 2, {}}; pos += 2; }
    }
  }

  DunToken t{dur, step, isRest, accidental, tied};
  t.ornament = ornament;
  return t;
}

// ---------------------------------------------------------------------------
// Parse header line: "key:D scale:Major meter:4/4 bpm:108"
// ---------------------------------------------------------------------------
inline DunHeader parse_header(const std::string& line) {
  DunHeader h;
  std::istringstream ss(line);
  std::string part;
  while (ss >> part) {
    auto colon = part.find(':');
    if (colon == std::string::npos) continue;
    std::string key = part.substr(0, colon);
    std::string val = part.substr(colon + 1);
    if (key == "key") h.keyName = val;
    else if (key == "scale") h.scaleName = val;
    else if (key == "meter") {
      auto slash = val.find('/');
      if (slash != std::string::npos) {
        h.meterNum = std::stoi(val.substr(0, slash));
        h.meterDen = std::stoi(val.substr(slash + 1));
      }
    }
    else if (key == "bpm") h.bpm = std::stof(val);
    else if (key == "start") {
      // Format: start:2 (just degree) or start:2:5 (degree:octave)
      auto colon2 = val.find(':');
      if (colon2 != std::string::npos) {
        h.startDegree = std::stoi(val.substr(0, colon2));
        h.startOctave = std::stoi(val.substr(colon2 + 1));
      } else {
        h.startDegree = std::stoi(val);
      }
    }
    else if (key == "pickup") h.pickupBeats = std::stof(val);
  }
  return h;
}

// ---------------------------------------------------------------------------
// Parse a full DUN file
// ---------------------------------------------------------------------------
inline DunParseResult parse_dun(const std::string& text) {
  DunParseResult result;
  std::istringstream stream(text);
  std::string line;
  bool headerParsed = false;

  // Collect all non-comment, non-empty tokens
  std::vector<std::string> allTokens;

  while (std::getline(stream, line)) {
    // Trim leading whitespace
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);

    // Skip comments
    if (line[0] == '#') continue;

    // First non-comment line is header
    if (!headerParsed) {
      result.header = parse_header(line);
      headerParsed = true;
      continue;
    }

    // Tokenize
    std::istringstream ls(line);
    std::string tok;
    while (ls >> tok) {
      allTokens.push_back(tok);
    }
  }

  // Split into phrases (on /) and figures (on |)
  std::vector<std::vector<DunToken>> currentPhrase;
  std::vector<DunToken> currentFigure;

  for (auto& tok : allTokens) {
    if (tok == "/") {
      if (!currentFigure.empty()) {
        currentPhrase.push_back(std::move(currentFigure));
        currentFigure.clear();
      }
      if (!currentPhrase.empty()) {
        result.phrases.push_back(std::move(currentPhrase));
        currentPhrase.clear();
      }
    } else if (tok == "|") {
      if (!currentFigure.empty()) {
        currentPhrase.push_back(std::move(currentFigure));
        currentFigure.clear();
      }
    } else {
      DunToken t = parse_token(tok);
      if (t.tied && !currentFigure.empty()) {
        // Merge tied duration into the previous token; don't emit a new one.
        // If the tied token carries an ornament, transfer it to the merged note.
        currentFigure.back().duration += t.duration;
        if (has_ornament(t.ornament)) {
          currentFigure.back().ornament = t.ornament;
        }
      } else {
        currentFigure.push_back(t);
      }
    }
  }

  // Flush remaining
  if (!currentFigure.empty())
    currentPhrase.push_back(std::move(currentFigure));
  if (!currentPhrase.empty())
    result.phrases.push_back(std::move(currentPhrase));

  return result;
}

// ---------------------------------------------------------------------------
// Load and parse a DUN file from disk
// ---------------------------------------------------------------------------
inline DunParseResult load_dun(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("Cannot open DUN file: " + path);
  std::string text((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
  return parse_dun(text);
}

// ---------------------------------------------------------------------------
// Convert DunParseResult to a Piece
//
// Strategy: the first token of each figure carries the inter-figure movement
// (step) in DUN. For fi>0, that step is absorbed directly into the first
// unit's step field (previously it was passed as a FigureConnector, which
// is no longer used). The first token of phrase-figure 0 is Xn (no movement),
// so the startingPitch carries over from the previous phrase.
// ---------------------------------------------------------------------------
inline Piece dun_to_piece(const DunParseResult& dun, int startOctaveOverride = -1) {
  Piece piece;

  // Key — normalize shorthand (Eb→E Flat, F#→F Sharp, etc.) for Key::get
  std::string keyName = dun.header.keyName;
  if (keyName.size() >= 2 && keyName.back() == 'b') {
    keyName = keyName.substr(0, keyName.size() - 1) + " Flat";
  } else if (keyName.size() >= 2 && keyName.back() == '#') {
    keyName = keyName.substr(0, keyName.size() - 1) + " Sharp";
  }
  std::string scaleType = (dun.header.scaleName == "Minor" ||
                           dun.header.scaleName == "NaturalMinor")
                          ? "Minor" : "Major";
  piece.key = Key::get(keyName + " " + scaleType);

  // Section
  float totalBeats = 0;
  for (auto& phrase : dun.phrases)
    for (auto& fig : phrase)
      for (auto& tok : fig)
        totalBeats += tok.duration;

  Meter meter = Meter::M_4_4;
  if (dun.header.meterNum == 3 && dun.header.meterDen == 4)
    meter = Meter::M_3_4;
  else if (dun.header.meterNum == 6 && dun.header.meterDen == 8)
    meter = Meter::M_6_8;
  else if (dun.header.meterNum == 5 && dun.header.meterDen == 4)
    meter = Meter::M_5_4;
  else if (dun.header.meterNum == 7 && dun.header.meterDen == 8)
    meter = Meter::M_7_8;

  // Scale — use the key's scale (already resolved by Key::get)
  Scale scale = piece.key.scale;
  piece.add_section(Section("Main", totalBeats, dun.header.bpm, meter, scale));

  // Build phrases using a PitchReader to track current position
  int oct = (startOctaveOverride >= 0) ? startOctaveOverride
          : (dun.header.startOctave >= 0) ? dun.header.startOctave : 4;
  PitchReader reader(scale);
  reader.set_pitch(oct, dun.header.startDegree);

  Part part;
  part.name = "melody";
  part.instrumentType = "melody";

  Passage passage;

  for (auto& phraseDun : dun.phrases) {
    Phrase phrase;

    // First token of phrase should be Xn (no movement) — use current pitch
    phrase.startingPitch = reader.get_pitch();

    for (int fi = 0; fi < int(phraseDun.size()); ++fi) {
      auto& figDun = phraseDun[fi];
      if (figDun.empty()) continue;

      MelodicFigure fig;
      int startIdx = 0;
      int connStep = 0;

      if (fi == 0) {
        // First figure in phrase — first token is Xn, step should be 0
        // Apply the step anyway (in case it's not n) by moving the reader
        if (figDun[0].step != 0) {
          reader.step(figDun[0].step);
          phrase.startingPitch = reader.get_pitch();
        }
        FigureUnit u;
        u.duration = figDun[0].duration;
        u.step = 0;
        u.rest = figDun[0].rest;
        u.accidental = figDun[0].accidental;
        u.ornament = figDun[0].ornament;
        fig.units.push_back(u);
        startIdx = 1;
      } else {
        // Subsequent figure — first token's step is the inter-figure movement.
        // Absorb it into the first unit's step field instead of a connector.
        connStep = figDun[0].rest ? 0 : figDun[0].step;
        if (!figDun[0].rest) reader.step(connStep);

        FigureUnit u;
        u.duration = figDun[0].duration;
        u.step = connStep;  // absorbed: was formerly passed as FigureConnector
        u.rest = figDun[0].rest;
        u.accidental = figDun[0].accidental;
        u.ornament = figDun[0].ornament;
        fig.units.push_back(u);
        startIdx = 1;
      }

      // Remaining tokens — accidentals are transient, PitchReader always
      // advances diatonically regardless of accidental
      for (int ti = startIdx; ti < int(figDun.size()); ++ti) {
        FigureUnit u;
        u.duration = figDun[ti].duration;
        u.step = figDun[ti].rest ? 0 : figDun[ti].step;
        u.rest = figDun[ti].rest;
        u.accidental = figDun[ti].accidental;
        u.ornament = figDun[ti].ornament;
        fig.units.push_back(u);
        if (!figDun[ti].rest) reader.step(figDun[ti].step);
      }

      phrase.add_melodic_figure(std::move(fig));  // single-arg: connector no longer used
    }

    passage.add_phrase(std::move(phrase));
  }

  part.passages["Main"] = std::move(passage);
  piece.parts.push_back(std::move(part));

  return piece;
}

} // namespace mforce
