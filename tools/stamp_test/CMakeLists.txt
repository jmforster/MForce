# Headless exerciser for the mforce_ui engine-stamp guard. Deliberately links
# nothing — it includes tools/mforce_ui/build_stamp.h directly — so it stays
# buildable and runnable while a running mforce_ui.exe holds itself locked
# against relinking, which is exactly when the guard needs checking.
add_executable(stamp_test main.cpp)

if (MSVC)
  target_compile_options(stamp_test PRIVATE /W3)
  target_compile_definitions(stamp_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
