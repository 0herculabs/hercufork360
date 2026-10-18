add_library(xenia-hid-sdl STATIC)
xe_platform_sources(xenia-hid-sdl ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xenia-hid-sdl PUBLIC xenia-base xenia-hid xenia-ui SDL2)
xe_target_defaults(xenia-hid-sdl)

if(XENIA_BUILD_WHEEL_TESTS)
  add_executable(xenia-wheel-calibration-test wheel_calibration_test.cc)
  xe_target_defaults(xenia-wheel-calibration-test)
  add_test(NAME wheel-calibration COMMAND xenia-wheel-calibration-test)
  add_executable(xenia-wheel-test sdl_wheel_test.cc sdl_wheel.cc)
  target_compile_definitions(xenia-wheel-test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(xenia-wheel-test PRIVATE xenia-base SDL2)
  xe_target_defaults(xenia-wheel-test)
  add_test(NAME wheel COMMAND xenia-wheel-test)
  add_executable(xenia-wheel-probe sdl_wheel_probe_main.cc)
  target_compile_definitions(xenia-wheel-probe PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(xenia-wheel-probe PRIVATE SDL2)
  xe_target_defaults(xenia-wheel-probe)
  add_executable(xenia-wheel-hook-test sdl_wheel_hook_test.cc)
  target_link_libraries(xenia-wheel-hook-test PRIVATE
    capstone fmt imgui xenia-base xenia-core xenia-cpu xenia-gpu
    xenia-kernel xenia-ui xenia-patcher xenia-hid-sdl
    $<$<BOOL:${XE_TARGET_X86_64}>:xenia-cpu-backend-x64>
    $<$<BOOL:${XE_TARGET_AARCH64}>:xenia-cpu-backend-a64>)
  xe_target_defaults(xenia-wheel-hook-test)
  add_test(NAME wheel-guest-hook COMMAND xenia-wheel-hook-test)
  set_tests_properties(wheel wheel-guest-hook PROPERTIES TIMEOUT 30)
endif()
