#pragma once
#include "MpChangeForms.h"
#include <chrono>
#include <optional>
#include <type_traits>

class MpObjectReference;

namespace ChangeFormGuard_ {
void RequestSave(MpObjectReference* self);
}

class ChangeFormGuard
{
public:
  ChangeFormGuard(const MpChangeForm& changeForm_, MpObjectReference* self_)
    : changeForm(changeForm_)
    , self(self_)
  {
  }

  enum class Mode
  {
    RequestSave,
    NoRequestSave
  };

  void EditChangeForm(std::function<void(MpChangeForm&)> f,
                      Mode mode = Mode::RequestSave)
  {
    f(changeForm);
    if (!blockSaving && mode == Mode::RequestSave) {
      lastSaveRequest = std::chrono::system_clock::now();
      ChangeFormGuard_::RequestSave(self);
    }
  }

  const MpChangeForm& ChangeForm() const noexcept { return changeForm; }

  auto GetLastSaveRequestMoment() const { return lastSaveRequest; }

  bool blockSaving = false;

protected:
  void CommitPreparedInventoryAndEquipment(Inventory&& inventory,
                                           Equipment&& equipment) noexcept
  {
    static_assert(std::is_nothrow_move_assignable_v<Inventory>);
    static_assert(std::is_nothrow_move_assignable_v<Equipment>);
    changeForm.inv = std::move(inventory);
    changeForm.equipment = std::move(equipment);
    changeForm.baseContainerAdded = true;
    lastSaveRequest = std::chrono::system_clock::now();
  }

  void CommitPreparedKnowledge(DynamicFields&& fields, LearnedSpells&& spells) noexcept
  {
    static_assert(std::is_nothrow_move_assignable_v<DynamicFields>);
    static_assert(std::is_nothrow_move_assignable_v<LearnedSpells>);
    changeForm.dynamicFields = std::move(fields);
    changeForm.learnedSpells = std::move(spells);
    lastSaveRequest = std::chrono::system_clock::now();
  }

  // Call only after queuing the matching full snapshot. Nothing after queue
  // publication may allocate or fail before the live state matches it.
  void CommitPreparedDynamicFields(DynamicFields&& fields) noexcept
  {
    static_assert(std::is_nothrow_move_assignable_v<DynamicFields>);
    changeForm.dynamicFields = std::move(fields);
    lastSaveRequest = std::chrono::system_clock::now();
  }

private:
  MpChangeForm changeForm;
  MpObjectReference* const self;
  std::optional<std::chrono::system_clock::time_point> lastSaveRequest;
};
