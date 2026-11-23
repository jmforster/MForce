# The editor: four modules made with mforce_module, the program mforce_editor,
# and the library targets they link (mforce_lib_<family>), built from the git
# submodules under third_party/.
#
# A plain clone leaves the submodule folders empty. The engine, the tools and
# every gate build without them; the editor is made only when one known file
# of each library it builds is there.

set(third_party ${PROJECT_SOURCE_DIR}/third_party)
set(missing "")
foreach(known imgui/imgui.cpp imnodes/imnodes.cpp glfw/CMakeLists.txt rtaudio/RtAudio.cpp)
  if(NOT EXISTS ${third_party}/${known})
    list(APPEND missing third_party/${known})
  endif()
endforeach()
if(missing)
  # A CMake list is one string with ";" between its items.
  string(REPLACE ";" ", " missing "${missing}")
  message(STATUS "MForce editor: not built (missing ${missing}); fetch them with: git submodule update --init")
  return()
endif()
# The SYSTEM target property below, which keeps the libraries' warnings out of
# our build, is read from CMake 3.25 on.
if(CMAKE_VERSION VERSION_LESS 3.25)
  message(STATUS "MForce editor: not built (CMake ${CMAKE_VERSION}; the editor needs 3.25 or later)")
  return()
endif()
message(STATUS "MForce editor: building (third_party found)")

# --- The libraries ----------------------------------------------------------
# A library's own sources are compiled with its own settings and never with
# mforce_compile_rules, so a warning inside one is printed and does not fail
# the build. Its headers reach our code as SYSTEM headers (MSVC reads them
# through /external:I at /external:W0), so a warning a library header raises
# inside one of our files is not reported either.

# The libraries' build options, as plain variables of this folder: a
# library's option() reads a plain variable of its name before its own default
# (policy CMP0077, made the default here for RtAudio, which asks for an older
# CMake), and nothing is written to the cache.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_INSTALL OFF)
set(RTAUDIO_BUILD_STATIC_LIBS ON)
set(RTAUDIO_BUILD_SHARED_LIBS OFF)
set(RTAUDIO_BUILD_TESTING OFF)
set(RTAUDIO_BUILD_PYTHON OFF)
# OFF: RtAudio uses the runtime library DLL (/MD), as every other target does.
set(RTAUDIO_STATIC_MSVCRT OFF)
# RtAudio puts a Debug postfix "d" in the cache when it finds none there, and
# every target of the tree would take it. An empty entry made first keeps a
# Debug build's file names as they are.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "Appended to the name of a target's Debug output")

# A source folder outside this one is added with the folder its build goes to.
add_subdirectory(${third_party}/glfw ${CMAKE_CURRENT_BINARY_DIR}/third_party/glfw)
add_subdirectory(${third_party}/rtaudio ${CMAKE_CURRENT_BINARY_DIR}/third_party/rtaudio)
set_target_properties(glfw rtaudio PROPERTIES SYSTEM ON)
find_package(OpenGL REQUIRED)

# GLFW and the system's OpenGL, for ui_app.
add_library(mforce_lib_glfw INTERFACE)
target_link_libraries(mforce_lib_glfw INTERFACE glfw OpenGL::GL)

# RtAudio, for ui_audio.
add_library(mforce_lib_rtaudio INTERFACE)
target_link_libraries(mforce_lib_rtaudio INTERFACE rtaudio)

# Dear ImGui's core (its demo window left out) and imnodes, for ui_views and
# ui_app. Neither ships a CMake target of its own.
add_library(mforce_lib_imgui STATIC
  ${third_party}/imgui/imgui.cpp
  ${third_party}/imgui/imgui_draw.cpp
  ${third_party}/imgui/imgui_tables.cpp
  ${third_party}/imgui/imgui_widgets.cpp
  ${third_party}/imnodes/imnodes.cpp)
target_include_directories(mforce_lib_imgui SYSTEM PUBLIC ${third_party}/imgui ${third_party}/imnodes)
# imnodes uses ImGui's arithmetic on ImVec2, which this switches on.
target_compile_definitions(mforce_lib_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ImGui's GLFW and OpenGL 3 backends, for ui_app only.
add_library(mforce_lib_imgui_backends STATIC
  ${third_party}/imgui/backends/imgui_impl_glfw.cpp
  ${third_party}/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(mforce_lib_imgui_backends SYSTEM PUBLIC ${third_party}/imgui/backends)
target_link_libraries(mforce_lib_imgui_backends PUBLIC mforce_lib_imgui mforce_lib_glfw)

# --- The modules, each after every module it uses -------------------------
add_subdirectory(ui_model)
add_subdirectory(ui_audio)
add_subdirectory(ui_control)
add_subdirectory(ui_views)
add_subdirectory(ui_app)
