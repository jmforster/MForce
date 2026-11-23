mforce_module(patch JSON
  SOURCES src/build_graph.cpp
          src/edit.cpp
          src/node_json.cpp
          src/patch.cpp
          src/patch_json.cpp
          src/retired_spellings.cpp
          src/table_rule.cpp
          src/type_declarations.cpp
  USES    core source)

mforce_tests(patch
  SOURCES tests/main.cpp
          tests/blep_tests.cpp
          tests/codec_refusal_tests.cpp
          tests/codec_tests.cpp
          tests/edit_census_tests.cpp
          tests/edit_tests.cpp
          tests/envelope_form_tests.cpp
          tests/graph_build_tests.cpp
          tests/patch_check_tests.cpp
          tests/patch_record_tests.cpp
          tests/preset_curve_label_tests.cpp
          tests/registry_setup.cpp
          tests/retired_spelling_tests.cpp
          tests/type_declaration_tests.cpp)
