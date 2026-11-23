#pragma once
#include <string>

namespace mforce::ui {

struct Session;

// An act that would lose unsaved changes asks first.
struct Unsaved {
  enum class Waiting { Nothing, Close, Open };
  Waiting     waiting{Waiting::Nothing};
  std::string path;              // the file an Open waits to open
  bool        closing{false};    // the answer was given: the program may close
};

enum class Answer { Save, Discard, Cancel };

// Opens `path` at once when nothing is unsaved; otherwise the Open waits for
// answer_unsaved.
void request_open(Session& s, const std::string& path);
// Sets closing at once when nothing is unsaved; otherwise the Close waits.
void request_close(Session& s);
// Save: saves when anything is unsaved, then does what waited; when the
// patch has no file, or the save fails, status says so and the wait goes
// on. Discard: does what waited. Cancel: forgets it.
void answer_unsaved(Session& s, Answer answer);

}  // namespace mforce::ui
