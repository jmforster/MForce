# The build rules every MForce target shares.
#
# A CMake "function" is called like a command: mforce_compile_rules(mforce_cli).
# It is the one place the language level and the warning rules are written,
# so no target sets its own.

# Gives <target> the compile rules of every MForce target:
#   - C++20, required (a compiler without it fails to configure), with no
#     compiler-specific language extensions;
#   - on MSVC: warning level 4, standard-conforming mode, and every warning
#     treated as an error (/WX), so a warning fails the build.
# The warning flags are written for MSVC only: no other compiler has built
# this tree, so nothing is claimed for one.
function(mforce_compile_rules target)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
  if(MSVC)
    # PRIVATE: these flags compile this target's own files and are not passed
    # on to the targets that link it.
    target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
  endif()
endfunction()
