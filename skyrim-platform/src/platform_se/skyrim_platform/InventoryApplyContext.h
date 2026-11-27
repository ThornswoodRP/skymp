#pragma once
#include <stdexcept>

namespace InventoryApplyContext {
namespace Detail {
inline thread_local bool update = false;
}
// Admission belongs to the existing JS update entry, not to the global VM
// pointer (which remains set while native work calls synchronous JS hooks).
class Scope {
public:
  explicit Scope(bool available) : previous(Detail::update) { Detail::update = available; }
  ~Scope() { Detail::update = previous; }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
private:
  bool previous;
};
inline bool Available() { return Detail::update; }
inline void RequireUpdate() {
  if (!Available()) throw std::logic_error("Inventory application requires an update callback, not a synchronous native hook");
}
}
