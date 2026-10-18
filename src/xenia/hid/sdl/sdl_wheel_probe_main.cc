// Read-only inventory and calibration aid. Never initializes SDL haptics.
#include <cstdio>
#include "SDL.h"
int main() {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) < 0) {
    return 1;
  }
  SDL_JoystickUpdate();
  const int count = SDL_NumJoysticks();
  std::printf("SDL2 joysticks: %d (read-only, no force output)\n", count);
  for (int i = 0; i < count; ++i) {
    auto* joystick = SDL_JoystickOpen(i);
    if (!joystick) {
      continue;
    }
    char guid[33];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid,
                              sizeof(guid));
    std::printf("%d: %s\n  GUID: %s; axes: %d; buttons: %d; hats: %d\n", i,
                SDL_JoystickName(joystick), guid, SDL_JoystickNumAxes(joystick),
                SDL_JoystickNumButtons(joystick),
                SDL_JoystickNumHats(joystick));
    SDL_JoystickUpdate();
    std::printf("  Raw axes:");
    for (int a = 0; a < SDL_JoystickNumAxes(joystick); ++a) {
      std::printf(" %d:%d", a, SDL_JoystickGetAxis(joystick, a));
    }
    std::puts("");
    SDL_JoystickClose(joystick);
  }
  SDL_Quit();
  return 0;
}
