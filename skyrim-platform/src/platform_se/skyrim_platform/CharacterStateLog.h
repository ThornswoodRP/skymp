#pragma once
// Thornswood #6886. Which way the game moves the person's own character, for
// the log, when it changes.
//
// A Main tester on 25 Nov 2026 reported that in first person looking up and
// walking forward lifted the character and looking down lowered it, stairs
// were hard to climb, and the character ended up stuck under a dock. Moving
// along the camera's pitch is how the game moves a character whose controller
// may pitch (kCanPitch), which it gives a swimming or flying character. The
// session logs held nothing that says which state the controller was in: the
// front writes the character's state only when jump is pressed, and only what
// Papyrus can read (isSwimming, a few graph variables). So the platform writes
// the controller's own state, the state it wants, and the flags that change
// how it moves, each time any of them changes, and whether the camera is in
// first person. A jump is three lines. A burst of changes is bounded so a
// stuck toggle cannot fill the log: kLinesPerWindow lines a minute at most,
// and the next line written says how many were left out.
//
// This holds no game types, so a test builds it alone.
#include <cstdint>

class CharacterStateLog
{
public:
  static constexpr int64_t kWindowMs = 60000;
  static constexpr int kLinesPerWindow = 30;

  // The CHARACTER_FLAGS bits that change how the controller moves the
  // character (bhkCharacterController.h): no gravity on ground (1), no
  // friction (3), no simulation (17), shoulders under water (23), can pitch
  // (25), can roll (26), no character collisions (27), swim at the water's
  // surface (31). The rest change every step and say nothing about this.
  static constexpr uint32_t kFlagsKept = (1u << 1) | (1u << 3) | (1u << 17) |
    (1u << 23) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 31);

  struct State
  {
    uint32_t current = 0; // hkpCharacterStateType: 0 on ground, 1 jumping,
                          // 2 in air, 3 climbing, 4 flying, 5 swimming
    uint32_t wanted = 0;
    uint32_t flags = 0; // kept bits only
    bool firstPerson = false;

    bool operator==(const State& o) const
    {
      return current == o.current && wanted == o.wanted && flags == o.flags &&
        firstPerson == o.firstPerson;
    }
  };

  // Whether s, read at now (milliseconds), is a line for the log. It is when
  // it differs from the state read before it and this minute's lines are not
  // spent. leftOut is set to how many changes were not written since the last
  // line that was.
  bool Write(State s, int64_t now, int* leftOut)
  {
    s.flags &= kFlagsKept;
    if (seen && s == last) {
      return false;
    }
    seen = true;
    last = s;
    if (now - windowStart >= kWindowMs) {
      windowStart = now;
      written = 0;
    }
    if (written >= kLinesPerWindow) {
      ++notWritten;
      return false;
    }
    ++written;
    if (leftOut) {
      *leftOut = notWritten;
    }
    notWritten = 0;
    return true;
  }

private:
  bool seen = false;
  State last;
  int64_t windowStart = 0;
  int written = 0;
  int notWritten = 0;
};
