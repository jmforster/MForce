set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/engine/third_party/imgui)
set(IMNODES_DIR ${CMAKE_SOURCE_DIR}/engine/third_party/imnodes)

add_executable(mforce_ui
  main.cpp

  # ImGui core
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp

  # ImGui backends
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp

  # imnodes
  ${IMNODES_DIR}/imnodes.cpp
)

target_include_directories(mforce_ui PRIVATE
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${IMNODES_DIR}
)

target_compile_definitions(mforce_ui PRIVATE IMGUI_DEFINE_MATH_OPERATORS)

target_link_libraries(mforce_ui PRIVATE
  mforce_engine
  glfw
  opengl32
)

if (MSVC)
  target_compile_options(mforce_ui PRIVATE /W3)
  set_target_properties(mforce_ui PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(mforce_ui PRIVATE /ENTRY:mainCRTStartup)
endif()
