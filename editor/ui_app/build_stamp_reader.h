#pragma once
#include "mforce/ui_model/build_stamp.h"

namespace mforce::ui {

// This program's build stamp, read at start: the time of its own file, the
// commit the repository has out, and the newest .h, .hpp or .cpp file of the
// modules it is built from and of editor/ui_app when it is newer than the
// program. The folders and the repository's `.git` were written into the
// program when the build was configured (build_sources.h). Empty when the
// program cannot find its own file.
BuildStamp read_build_stamp();

}  // namespace mforce::ui
