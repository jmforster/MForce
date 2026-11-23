# The engine: one folder and one target per module (mforce_module in
# cmake/MforceModule.cmake). A module is added after every module it uses.

# nlohmann/json.hpp as a SYSTEM header: a warning inside it is the library's,
# not ours, so it does not fail the build. MSVC reads it through /external:I.
# Only a module that names JSON is given it (and, through it, its users).
add_library(mforce_json INTERFACE)
target_include_directories(mforce_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# The test harness first: every module's test program links it.
add_subdirectory(testing)
add_subdirectory(core)
add_subdirectory(contract)
add_subdirectory(util)
add_subdirectory(source)
add_subdirectory(patch)
add_subdirectory(render)
add_subdirectory(music)
add_subdirectory(voicing)
add_subdirectory(compose)
add_subdirectory(shelved)
add_subdirectory(perform)
add_subdirectory(play)

# The name the tools link: no code of its own, every module but shelved.
add_library(mforce_engine INTERFACE)
target_link_libraries(mforce_engine INTERFACE
  mforce_core mforce_contract mforce_util mforce_source mforce_patch mforce_render
  mforce_music mforce_voicing mforce_compose mforce_perform mforce_play)
