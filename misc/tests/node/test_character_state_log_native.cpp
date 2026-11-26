// Thornswood #6886. CharacterStateLog, which decides when the person's own
// character controller state is a line for the log.
#include "CharacterStateLog.h"
#include <cstdint>
#include <iostream>

static int failed = 0;
static void Check(const char* name, bool ok)
{
  std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
  if (!ok)
    ++failed;
}

static CharacterStateLog::State S(uint32_t current, uint32_t wanted,
                                  uint32_t flags, bool first)
{
  CharacterStateLog::State s;
  s.current = current;
  s.wanted = wanted;
  s.flags = flags;
  s.firstPerson = first;
  return s;
}

int main()
{
  const uint32_t canPitch = 1u << 25;
  const uint32_t tryStep = 1u << 2; // changes every step, not kept
  {
    CharacterStateLog log;
    int left = -1;
    Check("the first state read is written",
          log.Write(S(0, 0, 0, true), 0, &left) && left == 0);
    Check("the same state again is not",
          !log.Write(S(0, 0, 0, true), 16, &left));
    Check("a flag that is not kept changes nothing",
          !log.Write(S(0, 0, tryStep, true), 32, &left));
    Check("swimming is written",
          log.Write(S(5, 5, canPitch, true), 48, &left));
    Check("on ground but still able to pitch is written",
          log.Write(S(0, 0, canPitch, true), 64, &left));
    Check("can pitch going is written",
          log.Write(S(0, 0, 0, true), 80, &left));
    Check("the view changing is written",
          log.Write(S(0, 0, 0, false), 96, &left));
    Check("a wanted state changing is written",
          log.Write(S(0, 4, 0, false), 112, &left));
  }
  {
    // A toggle every frame for two minutes writes 30 lines a minute and
    // says what it left out.
    CharacterStateLog log;
    int lines = 0, firstMinute = 0, saidLeft = 0;
    for (int64_t t = 0; t < 2 * CharacterStateLog::kWindowMs; t += 16) {
      int left = 0;
      const bool odd = (t / 16) % 2;
      if (log.Write(S(odd ? 2 : 0, 0, 0, true), t, &left)) {
        ++lines;
        if (t < CharacterStateLog::kWindowMs)
          ++firstMinute;
        if (left > 0)
          saidLeft = left;
      }
    }
    Check("a burst writes no more than the minute's lines",
          firstMinute == CharacterStateLog::kLinesPerWindow);
    Check("and the next minute's lines again",
          lines == 2 * CharacterStateLog::kLinesPerWindow);
    Check("the first line of the next minute says how many were left out",
          saidLeft > 3000);
  }
  return failed ? 1 : 0;
}
