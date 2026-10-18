#ifndef XENIA_APP_WHEEL_CONFIG_DIALOG_H_
#define XENIA_APP_WHEEL_CONFIG_DIALOG_H_

#include <array>
#include <functional>
#include <string>
#include <vector>
#include "SDL.h"
#include "xenia/hid/sdl/wheel_calibration.h"
#include "xenia/ui/imgui_dialog.h"

namespace xe::app {
class EmulatorWindow;
class WheelConfigDialog final : public ui::ImGuiDialog {
 public:
  WheelConfigDialog(ui::ImGuiDrawer* drawer, EmulatorWindow& window,
                    std::function<void()> closed);
  ~WheelConfigDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Refresh();
  void Select(int index);
  void Poll();
  void Save();
  void DrawControls();
  EmulatorWindow& window_;
  std::function<void()> closed_;
  bool initialized_ = false;
  bool restore_gamepad_navigation_ = false;
  SDL_Joystick* joystick_ = nullptr;
  struct Device {
    int index;
    std::string name;
  };
  std::vector<Device> devices_;
  std::string selected_, status_;
  std::vector<int> axes_;
  std::vector<bool> buttons_;
  std::array<int, 15> bindings_{};
  int binding_ = -1;
  hid::sdl::WheelCalibration calibration_;
  hid::sdl::WheelCalibrationWizard wizard_;
  bool ffb_ = false, inverted_ = false;
  int gain_ = 10;
};
}  // namespace xe::app
#endif  // XENIA_APP_WHEEL_CONFIG_DIALOG_H_
