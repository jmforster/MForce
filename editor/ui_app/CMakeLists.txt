# mforce_editor, the editor's program. WIN32 makes it a Windows GUI program,
# which opens no console window; /ENTRY:mainCRTStartup lets such a program
# start at main, as a console program does, instead of at WinMain.
add_executable(mforce_editor WIN32
  main.cpp
  app_loop.cpp
  app_window.cpp
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
