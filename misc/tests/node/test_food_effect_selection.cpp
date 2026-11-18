#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>
#if __has_include("libespm/TemperatureFood.h")
#include "libespm/TemperatureFood.h"
#endif

namespace espm {
enum class ActorValue { FrostResist, Health, Stamina, None };
struct Effects { struct Effect { uint32_t effectId; float magnitude; int duration; }; };
struct ALCH {};
struct MGEF {};
}
using Effect = espm::Effects::Effect;
struct Slot { uint32_t ToId(uint32_t id) const { return 0x45000000u | id; } };
struct LoadOrder : std::vector<std::string> {
  std::optional<int> FindByFileName(std::string_view file) const {
    return file == "WindhelmSSE.esp" ? std::optional<int>(0) : std::nullopt;
  }
  Slot GetSlot(int) const { return {}; }
};
struct Lookup { uint32_t ToGlobalId(uint32_t id) const { return id + 100; } };
struct WorldState {
  LoadOrder espmFiles;
  std::vector<Effect> effects;
  WorldState& GetEspm() { return *this; }
  WorldState& GetBrowser() { return *this; }
  Lookup LookupById(uint32_t) { return {}; }
};
namespace espm {
template<class T> auto GetData(uint32_t id, WorldState* world) {
  if constexpr (std::is_same_v<T, ALCH>) {
    struct Data { std::vector<Effect> effects; };
    return Data{world->effects};
  } else {
    struct Data { struct { ActorValue primaryAV; } data; };
    return Data{{id == 101 ? ActorValue::FrostResist : id == 102 ? ActorValue::Health : ActorValue::Stamina}};
  }
}
}
struct Actor {
  std::vector<Effect> applied;
  void ApplyMagicEffects(const std::vector<Effect>& effects, bool) { applied = effects; }
};
struct EatItemEvent {
  Actor* actor;
  uint32_t baseId;
  bool isAlchemyItem = true, isIngredient = false;
  float effectScale = 1.f;
  std::optional<std::vector<Effect>> madeEffects;
  void OnFireSuccess(WorldState*);
};
#include "callback.inc"

int main() {
  WorldState world;
  int failures = 0;
  const auto check = [&](const char* name, uint32_t id, std::vector<Effect> effects,
                         size_t count, float magnitude, int duration, float scale = 1.f) {
    Actor actor;
    world.effects = effects;
    EatItemEvent event{&actor, id};
    event.effectScale = scale;
    event.OnFireSuccess(&world);
    bool pass = actor.applied.size() == count;
    if (pass && count) {
      pass = actor.applied[0].magnitude == magnitude && actor.applied[0].duration == duration;
      if (count == 1) pass = pass && (actor.applied[0].effectId == 1 || actor.applied[0].effectId == 101);
    }
    std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
    failures += !pass;
  };
  check("Frostwheat Bread", 0x4512010f, {{1, 5, 60}, {2, 3, 0}}, 1, 5, 60);
  check("Sack of Frostwheat Flour", 0x4512f9c4, {{2, 1, 0}, {1, 3, 60}}, 1, 3, 60);
  check("Snowberry Juice", 0x4517167b, {{3, 5, 0}, {1, 5, 30}}, 1, 5, 30);
  check("Temperature-only", 0x4517167b, {{1, 5, 30}}, 1, 5, 30);
  check("Same local ID in another plugin", 0x4617167b, {{1, 5, 30}, {3, 5, 0}}, 2, 5, 30);
  check("Zero scale", 0x4517167b, {{1, 5, 30}, {3, 5, 0}}, 0, 0, 0, 0.f);
  return failures ? 1 : 0;
}
