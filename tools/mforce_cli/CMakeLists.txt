add_executable(mforce_cli main.cpp explore.cpp)
target_link_libraries(mforce_cli PRIVATE mforce_engine)

if (MSVC)
  target_compile_options(mforce_cli PRIVATE /W4 /permissive- /bigobj)
endif()
