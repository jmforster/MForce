add_executable(mforce_cli main.cpp explore.cpp)
target_link_libraries(mforce_cli PRIVATE mforce_engine)
mforce_compile_rules(mforce_cli)
# A Debug build of main.cpp comes near MSVC's default limit on COFF sections;
# the large-object format lifts the limit.
target_compile_options(mforce_cli PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

# With the shelved option on, the tool registers the shelved strategies:
# MFORCE_WITH_SHELVED guards the include and the calls in main.cpp.
if(MFORCE_SHELVED)
  target_link_libraries(mforce_cli PRIVATE mforce_shelved)
  target_compile_definitions(mforce_cli PRIVATE MFORCE_WITH_SHELVED)
endif()
