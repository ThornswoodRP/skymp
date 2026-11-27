#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

// THORNSWOOD (Thornswood #4511). What runScriptFunction (ScriptCallApi.cpp)
// keeps between starting a call on the Papyrus VM and the VM saying it has
// returned: one argument as the plugin gave it, and the Promises still
// waiting, by id. Kept apart from the game's types so it can be tested alone
// (misc/tests/node/test_run_script_function.js).

// A string or a boolean, the only kinds the calls take.
struct ScriptCallArg
{
  bool isString = false;
  std::string text;
  bool flag = false;
};

// The Promises waiting for their call to return. Touched on the JS thread
// only. Ids never repeat, not even after Clear, so a call that returns after
// the plugins were reloaded finds nothing to settle.
template <class Resolve>
class ScriptCallWaiting
{
public:
  uint64_t Add(Resolve resolve)
  {
    const uint64_t id = ++last;
    waiting.emplace(id, std::move(resolve));
    return id;
  }

  // The Promise for `id`, once; nothing for an id already settled or let go.
  std::optional<Resolve> Take(uint64_t id)
  {
    auto it = waiting.find(id);
    if (it == waiting.end()) {
      return std::nullopt;
    }
    std::optional<Resolve> out(std::move(it->second));
    waiting.erase(it);
    return out;
  }

  void Clear() { waiting.clear(); }

  size_t Waiting() const { return waiting.size(); }

private:
  uint64_t last = 0;
  std::unordered_map<uint64_t, Resolve> waiting;
};
