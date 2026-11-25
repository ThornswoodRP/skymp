#include <array>
#include <iostream>
#include <vector>

struct NiPoint3 {
  float x = 0, y = 0, z = 0;
  NiPoint3() = default;
  NiPoint3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  bool operator==(const NiPoint3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const NiPoint3& o) const { return !(*this == o); }
};
struct LocationalData { NiPoint3 pos, rot; int cell = 1; };
struct MpObjectReference {
  virtual ~MpObjectReference() = default;
  LocationalData state;
  std::vector<LocationalData> client[2];
  const NiPoint3& GetPos() const { return state.pos; }
  const NiPoint3& GetAngle() const { return state.rot; }
  int GetCellOrWorld() const { return state.cell; }
  void SetCellOrWorld(int cell) { state.cell = cell; }
  void SetPos(NiPoint3 pos) { state.pos = pos; }
  void SetAngle(NiPoint3 rot) { state.rot = rot; }
  void SendPlacedTransform() { for (auto& packets : client) packets.push_back(state); }
};
struct MpActor : MpObjectReference { void Teleport(LocationalData data) { state = data; } };
struct VarValue {
  MpObjectReference* ref = nullptr;
  double number = 0;
  VarValue(MpObjectReference* ref_) : ref(ref_) {}
  VarValue(int number_) : number(number_) {}
  static VarValue None() { return 0; }
  double CastToFloat() const { return number; }
  bool CastToBool() const { return number != 0; }
};
template<class T> T* GetFormPtr(VarValue v) { return dynamic_cast<T*>(v.ref); }
namespace spdlog { void error(const char*) {} }
struct PapyrusObjectReference {
  VarValue SetAngle(VarValue, const std::vector<VarValue>&);
  VarValue MoveTo(VarValue, const std::vector<VarValue>&);
};
#include "under-test.inc"

int main() {
  PapyrusObjectReference papyrus;
  MpObjectReference board;
  MpActor steward;
  steward.state.pos = {10, 20, 30};
  papyrus.SetAngle(&board, {0, 0, 90});
  papyrus.MoveTo(&board, {&steward, 1, 2, 3, 0});
  for (const auto& packets : board.client) {
    if (packets.size() != 2 || packets[0].rot != NiPoint3(0, 0, 90) ||
        packets[1].pos != NiPoint3(11, 22, 33) || packets[1].rot != packets[0].rot) {
      std::cerr << "FAIL: a subscribed client did not receive rotation and movement\n";
      return 1;
    }
  }
  std::cout << "PASS: both subscribed clients received rotation and movement\n";
}
