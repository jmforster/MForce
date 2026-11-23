add_executable(mforce_cli main.cpp explore.cpp)
target_link_libraries(mforce_cli PRIVATE mforce_engine mforce_shelved)
mforce_compile_rules(mforce_cli)
# A Debug build of main.cpp comes near MSVC's default limit on COFF sections;
# the large-object format lifts the limit.
target_compile_options(mforce_cli PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
