#pragma once
#include "MpChangeForms.h"
#include <database_drivers/IDatabase.h>
#include <spdlog/spdlog.h>

class FileDatabase
  : public Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  FileDatabase(std::string directory_,
               std::shared_ptr<spdlog::logger> logger_);

  void Iterate(const IterateCallback& iterateCallback,
               std::optional<std::vector<FormDesc>> filter) override;

  // How many .json files the last Iterate could not read as a change form.
  // Iterate logs each one and goes on; a migration must not (#6237).
  size_t GetUnreadFilesOnLastIterate() const noexcept;

private:
  std::vector<std::optional<MpChangeForm>>&& UpsertImpl(
    std::vector<std::optional<MpChangeForm>>&& changeForms,
    size_t& outNumUpserted) override;

  struct Impl;
  std::shared_ptr<Impl> pImpl;
};
