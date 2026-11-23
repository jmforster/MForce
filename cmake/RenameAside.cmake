# Run by the build just before it links a program:
#
#   cmake -DEXE=<path to the .exe> -P cmake/RenameAside.cmake
#
# Windows lets a running program's .exe be renamed but not overwritten or
# deleted. So an existing <name>.exe is deleted, and when it is running and
# cannot be, it is moved aside as <name>.old-<random>.exe; either way the link
# writes a new one. Copies moved aside by earlier builds are deleted first,
# each one no running program holds; one still held stays until a later build.
get_filename_component(dir "${EXE}" DIRECTORY)
get_filename_component(stem "${EXE}" NAME_WE)

file(GLOB old_copies "${dir}/${stem}.old-*.exe")
foreach(file IN LISTS old_copies EXE)
  # "cmake -E rm" fails on a file a running program holds; execute_process
  # reports that in a variable instead of stopping this script.
  execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${file}" RESULT_VARIABLE ignored ERROR_QUIET)
endforeach()

if(EXISTS "${EXE}")
  string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef tag)
  file(RENAME "${EXE}" "${dir}/${stem}.old-${tag}.exe")
endif()
