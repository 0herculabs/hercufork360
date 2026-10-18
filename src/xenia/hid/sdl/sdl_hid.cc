/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/sdl/sdl_hid.h"

#include "xenia/hid/sdl/sdl_input_driver.h"
#include "xenia/base/cvar.h"

DECLARE_string(wheel_name);

namespace xe {
namespace hid {
namespace sdl {

std::unique_ptr<InputDriver> Create(xe::ui::Window* window,
                                    size_t window_z_order) {
  return std::make_unique<SDLInputDriver>(window, window_z_order);
}

std::unique_ptr<InputDriver> CreateWheel(xe::ui::Window* window,
                                         size_t window_z_order) {
  if (cvars::wheel_name.empty()) {
    cvars::wheel_name = "Gudsen R3 Racing Wheel and Pedals";
  }
  return Create(window, window_z_order);
}

}  // namespace sdl
}  // namespace hid
}  // namespace xe
