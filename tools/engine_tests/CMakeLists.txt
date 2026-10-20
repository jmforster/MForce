add_executable(engine_tests main.cpp)
target_link_libraries(engine_tests PRIVATE mforce_engine)
# One translation unit holds every engine test; past MSVC's default COFF
# section limit it needs the large-object format.
target_compile_options(engine_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
