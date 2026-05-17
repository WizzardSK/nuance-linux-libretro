// InputManager implementation for Linux - keyboard only (no SDL2 dependency)
#ifndef _WIN32

#include "InputManager.h"
#include "joystick.h"
#include "NuonEnvironment.h"
#include <cstdio>
#include <cstring>

extern NuonEnvironment nuonEnv;
extern void DvdPlayerActive_SendNavInput(int code);

InputManager::~InputManager() {}

// External override from NUANCE_BTN_QUEUE: scripted button mask that's
// OR'd into the keyboard state so MessagePump's UpdateState doesn't
// overwrite scripted presses with the zero keyboard state.
uint16 g_btnQueueMask = 0;

class InputManagerImpl : public InputManager
{
private:
  unsigned int whichController;
  uint16 keyButtons;

public:
  InputManagerImpl() : InputManager(), whichController(1), keyButtons(0) {}
  ~InputManagerImpl() {}

  bool Init(HWND) override { return true; }
  void Activate() override {}
  bool SetJoystick(size_t) override { return true; }

  // Apply state to BOTH controller slots. Games differ on which they read
  // for player 1 — Tempest 3000's gameplay polls controller[0].buttons
  // (0x807FFF74), other titles use controller[1]. Writing to both
  // simultaneously is harmless (a single-player real-HW would only have
  // one controller anyway, so both slots reading the same input is the
  // expected behaviour). 'Z' key flip-flop preserved for advanced cases.
  void ApplyBoth(CONTROLLER_CALLBACK applyState, uint16 mask) {
    if (!applyState) return;
    applyState(0, mask);
    applyState(1, mask);
  }

  void UpdateState(CONTROLLER_CALLBACK applyState, ANYPRESSED_CALLBACK, void*) override {
    ApplyBoth(applyState, keyButtons | g_btnQueueMask);
  }

  void keyDown(CONTROLLER_CALLBACK applyState, int16 vkey) override {
    const int bitNum = nuonEnv.GetCTRLRBitnumFromMapping(ControllerButtonMapping(KEY, vkey, 0));
    if (bitNum >= 0) {
      const uint16 prev = keyButtons;
      keyButtons |= 1 << bitNum;
      // Edge-trigger DVD nav input (rising edges only — no auto-repeat).
      const uint16 edge = keyButtons & ~prev;
      if (edge & (1 << CTRLR_BITNUM_DPAD_UP))    DvdPlayerActive_SendNavInput(0);
      if (edge & (1 << CTRLR_BITNUM_DPAD_DOWN))  DvdPlayerActive_SendNavInput(1);
      if (edge & (1 << CTRLR_BITNUM_DPAD_LEFT))  DvdPlayerActive_SendNavInput(2);
      if (edge & (1 << CTRLR_BITNUM_DPAD_RIGHT)) DvdPlayerActive_SendNavInput(3);
      if (edge & (1 << CTRLR_BITNUM_BUTTON_A))   DvdPlayerActive_SendNavInput(4);
      if (edge & (1 << CTRLR_BITNUM_BUTTON_B))   DvdPlayerActive_SendNavInput(5);
    }
    ApplyBoth(applyState, keyButtons | g_btnQueueMask);
  }

  void keyUp(CONTROLLER_CALLBACK applyState, int16 vkey) override {
    const int bitNum = nuonEnv.GetCTRLRBitnumFromMapping(ControllerButtonMapping(KEY, vkey, 0));
    if (bitNum >= 0) keyButtons &= ~(1 << bitNum);
    if (vkey == 'Z') whichController = 1 - whichController;
    ApplyBoth(applyState, keyButtons | g_btnQueueMask);
  }

  bool GrabJoystick(HWND, size_t) override { return false; }
  void UngrabJoystick() override {}
  const Joystick* EnumJoysticks(size_t* pNumJoysticks) override { *pNumJoysticks = 0; return nullptr; }
};

InputManager* InputManager::Create() { return new InputManagerImpl(); }

bool InputManager::StrToInputType(const char* str, InputType* type)
{
  if (!strcasecmp("KEY", str)) { *type = KEY; return true; }
  if (!strcasecmp("JOYPOV", str)) { *type = JOYPOV; return true; }
  if (!strcasecmp("JOYAXIS", str)) { *type = JOYAXIS; return true; }
  if (!strcasecmp("JOYBUT", str)) { *type = JOYBUT; return true; }
  return false;
}

const char* InputManager::InputTypeToStr(InputType type)
{
  switch (type) {
    case KEY: return "KEY";
    case JOYPOV: return "JOYPOV";
    case JOYAXIS: return "JOYAXIS";
    case JOYBUT: return "JOYBUT";
    default: return "<unknown>";
  }
}

#endif
