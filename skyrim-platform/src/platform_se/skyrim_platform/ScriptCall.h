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
// waiting, by id. And what setScriptVariable takes into a script's variable:
// which JavaScript value fits which kind of script value. Kept apart from the
// game's types so it can be tested alone
// (misc/tests/node/test_run_script_function_native.js).

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

// The kinds of script value setScriptVariable writes, and the kinds of
// JavaScript value it is given. A form is an Object; null or undefined is
// Nothing, which an Object takes as None.
enum class ScriptValueKind
{
  Int,
  Float,
  Bool,
  String,
  Object,
  Other
};

enum class JsValueKind
{
  Number,
  Boolean,
  String,
  Object,
  Nothing,
  Other
};

// Why a value of kind `have` cannot go in a script value of kind `want`, or
// nothing when it can. Nothing is written unless every value fits.
inline std::string ScriptValueMismatch(ScriptValueKind want, JsValueKind have)
{
  switch (want) {
    case ScriptValueKind::Int:
    case ScriptValueKind::Float:
      return have == JsValueKind::Number ? "" : "a number is wanted";
    case ScriptValueKind::Bool:
      return have == JsValueKind::Boolean ? "" : "a boolean is wanted";
    case ScriptValueKind::String:
      return have == JsValueKind::String ? "" : "a string is wanted";
    case ScriptValueKind::Object:
      return have == JsValueKind::Object || have == JsValueKind::Nothing
        ? ""
        : "a form, or null for None, is wanted";
    default:
      return "it is a kind of value this does not write";
  }
}

// Why `given` values cannot fill a script array of `size`, or nothing when
// they can. An array is filled in place, so it keeps its own size; a script
// array that is None has no place to fill.
inline std::string ScriptArrayMismatch(bool arrayExists, size_t size,
                                       bool givenArray, size_t given)
{
  if (!arrayExists) {
    return "the array is None, so there is nothing to fill";
  }
  if (!givenArray) {
    return "it is an array, so an array is wanted";
  }
  if (given != size) {
    return "it holds " + std::to_string(size) + " values and " +
      std::to_string(given) + " were given";
  }
  return "";
}
