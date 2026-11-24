// mforce_editor, the patch editor.
//
//   mforce_editor [patch.json] [--frames N] [--shot FILE] [--select NAME] [--tab NAME]
//                 [--generate] [--pane NAME] [--columns N] [--popout NAME] [--collapse] [--listen NAME]
//
// Run from the repository root. A patch named is opened at start; with none,
// a new patch is shown, as File > New gives. --select NAME selects that node
// or face before the first frame, and --tab NAME (note, passage, chords,
// drums) the transport tab shown (both for pictures); the rest set up the
// pane for a picture (options.h). --frames N
// draws N frames and exits with 0, writing nothing but the --shot picture of
// the last frame. A failure is shown in a message box ("could not start"
// before the window is up, "stopped" after) and the exit code is 1.
#include <random>
#include <stdexcept>
#include <string>
#include "app_loop.h"
#include "app_window.h"
#include "error_box.h"
#include "options.h"
#include "settings_folder.h"
#include "win32_file_dialogs.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/folder_memory.h"
#include "mforce/ui_model/recent_files.h"
#include "mforce/ui_views/editor_views.h"

int main(int argc, char** argv) {
  using namespace mforce::ui;
  bool windowUp = false;
  try {
    const Options options = read_options(argc, argv);
    const SettingsFiles files = options.frames < 0 ? settings_files() : SettingsFiles{};
    AppWindow window(1400, 900, "MForce", files.imguiIni);
    windowUp = true;
    Session session;
    session.seeds.seed(std::random_device{}());   // a new node's seed is random, kept in its file
    session.audio.open();                           // before any player is built: its rate is final here
    if (!files.recentFiles.empty()) session.recent = read_recent_files(files.recentFiles);
    if (options.patch.empty()) new_file(session);
    else open_file(session, options.patch);
    if (!options.select.empty()) select_node(session, options.select);
    if (!options.tab.empty()) set_transport(session, with_tab(session.transport, options.tab));
  apply_picture_options(session, options);
    FolderMemory folders = files.folders.empty() ? FolderMemory{} : read_folder_memory(files.folders);
    Win32FileDialogs dialogs(folders);
    EditorViews views(dialogs);
    run(window, session, views, options);
    if (!files.recentFiles.empty()) write_recent_files(session.recent, files.recentFiles);
    if (!files.folders.empty()) write_folder_memory(folders, files.folders);
    return 0;
  } catch (const std::exception& e) {
    show_error_box(windowUp ? "MForce stopped" : "MForce could not start", e.what());
    return 1;
  }
}
