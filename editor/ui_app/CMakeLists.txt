# mforce_editor, the editor's program. WIN32 makes it a Windows GUI program,
# which opens no console window; /ENTRY:mainCRTStartup lets such a program
# start at main, as a console program does, instead of at WinMain.
add_executable(mforce_editor WIN32
  main.cpp
  app_loop.cpp
  app_window.cpp
  build_stamp_reader.cpp
  error_box.cpp
  options.cpp
  screenshot.cpp
  settings_folder.cpp
  system_clipboard.cpp
  win32_file_dialogs.cpp)
mforce_compile_rules(mforce_editor)
target_link_libraries(mforce_editor PRIVATE
  mforce_ui_model mforce_ui_audio mforce_ui_control mforce_ui_views
  mforce_lib_imgui_backends mforce_lib_glfw)
if(MSVC)
  target_link_options(mforce_editor PRIVATE /ENTRY:mainCRTStartup)
endif()
# The Windows open and save dialogs.
if(WIN32)
  target_link_libraries(mforce_editor PRIVATE comdlg32)
endif()

# Windows lets a running program's .exe be renamed but not overwritten: just
# before each link, an existing mforce_editor.exe is deleted, or moved aside
# when it is running, so a build succeeds while the editor runs
# (cmake/RenameAside.cmake).
add_custom_command(TARGET mforce_editor PRE_LINK
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:mforce_editor> -P ${PROJECT_SOURCE_DIR}/cmake/RenameAside.cmake
  VERBATIM)

# The build stamp's sources (build_stamp_reader.h): the include and src
# folders of every module the program is built from, found by walking what it
# links (the library targets mforce_lib_* and mforce_json are not modules),
# then this folder; and the repository's .git. They are written at configure
# time into build_sources.h, in this target's own build folder. The header is
# rewritten only when its text changes, so an unchanged list rebuilds nothing.
get_target_property(pending mforce_editor LINK_LIBRARIES)
set(seen "")
set(folders "")
while(pending)
  list(POP_FRONT pending name)
  if(NOT name MATCHES "^mforce_" OR name MATCHES "^mforce_lib_" OR name STREQUAL "mforce_json"
     OR name IN_LIST seen OR NOT TARGET ${name})
    continue()
  endif()
  list(APPEND seen ${name})
  get_target_property(dir ${name} SOURCE_DIR)
  list(APPEND folders ${dir}/include ${dir}/src)
  # A module's uses are on its INTERFACE_LINK_LIBRARIES; a library that
  # compiles files of its own (not an INTERFACE one) also has LINK_LIBRARIES.
  get_target_property(used ${name} INTERFACE_LINK_LIBRARIES)
  if(used)
    list(APPEND pending ${used})
  endif()
  get_target_property(kind ${name} TYPE)
  if(NOT kind STREQUAL "INTERFACE_LIBRARY")
    get_target_property(used ${name} LINK_LIBRARIES)
    if(used)
      list(APPEND pending ${used})
    endif()
  endif()
endwhile()
list(SORT folders)
list(APPEND folders ${CMAKE_CURRENT_SOURCE_DIR})
# R"(...)" is a C++ raw string: a path is written as it is, with nothing escaped.
set(sources_text "// Written by editor/ui_app/CMakeLists.txt when the build is configured: the folders
")
string(APPEND sources_text "// whose sources the build stamp counts, and the repository's .git.
#pragma once

")
string(APPEND sources_text "namespace mforce::ui::build_sources {

inline constexpr const char* kFolders[] = {
")
foreach(folder IN LISTS folders)
  string(APPEND sources_text "  R\"(${folder})\",
")
endforeach()
string(APPEND sources_text "};

inline constexpr const char* kGit = R\"(${PROJECT_SOURCE_DIR}/.git)\";

")
string(APPEND sources_text "}  // namespace mforce::ui::build_sources
")
set(sources_header ${CMAKE_CURRENT_BINARY_DIR}/generated/build_sources.h)
set(written "")
if(EXISTS ${sources_header})
  file(READ ${sources_header} written)
endif()
if(NOT written STREQUAL sources_text)
  file(WRITE ${sources_header} "${sources_text}")
endif()
target_include_directories(mforce_editor PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
