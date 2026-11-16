#include "MigrationDatabase.h"
#include "FileDatabase.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <spdlog/spdlog.h>

// Thornswood #6237: the move of a world from the file driver to MongoDB.
//
// It copies every change form of databaseOld into databaseNew, then reads
// databaseNew back and compares it with what it read from databaseOld, form
// by form, as the engine writes them (MpChangeForm::ToJson). It succeeds, and
// the server exits with 0, only when the two hold the same forms with the
// same content. Everything else throws, so the server exits with an error and
// says why, and the tool that runs it keeps the file world:
//   - databaseNew already holds forms (a second run must not mix two worlds);
//   - databaseOld holds none (a wrong name or folder is not an empty world);
//   - a file of the old world could not be read as a change form (the file
//     driver logs those and goes on, which here would drop that form);
//   - the old world names one form twice;
//   - the new world, read back, lacks a form, has one more, or has one whose
//     content differs.
// Upstream exited with 0 on the first two as well, so a tool could not tell
// a move from a skip.

namespace {
using Database =
  Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>;

size_t CountChangeForms(const std::shared_ptr<Database>& database)
{
  size_t n = 0;
  database->Iterate([&](const MpChangeForm&) { ++n; }, std::nullopt);
  return n;
}

std::string Dump(const MpChangeForm& changeForm)
{
  return MpChangeForm::ToJson(changeForm).dump();
}

std::string FirstFew(const std::vector<std::string>& names)
{
  std::string res;
  for (size_t i = 0; i < names.size() && i < 5; ++i) {
    res += (i ? ", " : "") + names[i];
  }
  if (names.size() > 5) {
    res += fmt::format(" and {} more", names.size() - 5);
  }
  return res;
}
}

struct MigrationDatabase::Impl
{
  std::shared_ptr<Database> newDatabase;
  std::shared_ptr<Database> oldDatabase;
  std::function<void()> exit;
  std::function<void()> terminate;
};

MigrationDatabase::MigrationDatabase(std::shared_ptr<Database> newDatabase,
                                     std::shared_ptr<Database> oldDatabase,
                                     std::function<void()> exit,
                                     std::function<void()> terminate)
{
  pImpl.reset(new Impl{ newDatabase, oldDatabase, exit, terminate });

  spdlog::info("MigrationDatabase: verifying newDatabase emptiness");

  if (size_t n = CountChangeForms(newDatabase); n > 0) {
    throw std::runtime_error(fmt::format(
      "MigrationDatabase: databaseNew already holds {} change forms, so "
      "nothing was moved",
      n));
  }

  spdlog::info("MigrationDatabase: reading oldDatabase");

  std::map<std::string, std::string> oldForms;
  std::vector<MpChangeForm> changeForms;
  std::vector<std::string> twice;

  oldDatabase->Iterate(
    [&](const MpChangeForm& changeForm) {
      auto desc = changeForm.formDesc.ToString();
      if (!oldForms.emplace(desc, Dump(changeForm)).second) {
        twice.push_back(desc);
        return;
      }
      changeForms.push_back(changeForm);
      if (changeForms.size() % 1000 == 0) {
        spdlog::info("MigrationDatabase: read {} changeforms",
                     changeForms.size());
      }
    },
    std::nullopt);

  if (auto file = std::dynamic_pointer_cast<FileDatabase>(oldDatabase)) {
    if (size_t unread = file->GetUnreadFilesOnLastIterate(); unread > 0) {
      throw std::runtime_error(fmt::format(
        "MigrationDatabase: {} file(s) of the old world could not be read as "
        "change forms (each is named above), so nothing was moved",
        unread));
    }
  }
  if (!twice.empty()) {
    throw std::runtime_error(fmt::format(
      "MigrationDatabase: the old world names {} form(s) twice: {}",
      twice.size(), FirstFew(twice)));
  }
  if (changeForms.empty()) {
    throw std::runtime_error(
      "MigrationDatabase: databaseOld holds no change forms, so nothing was "
      "moved; check its databaseName");
  }

  spdlog::info("MigrationDatabase: upserting {} changeforms into the new "
               "database",
               changeForms.size());

  const size_t chunkSize = 1000;
  size_t totalUpserted = 0;
  for (size_t i = 0; i < changeForms.size(); i += chunkSize) {
    std::vector<std::optional<MpChangeForm>> chunk;
    size_t end = std::min(i + chunkSize, changeForms.size());
    std::copy(changeForms.begin() + i, changeForms.begin() + end,
              std::back_inserter(chunk));

    size_t numUpserted = newDatabase->Upsert(std::move(chunk));
    totalUpserted += numUpserted;

    spdlog::info("MigrationDatabase: upserted chunk {}/{} ({} changeForms)",
                 (i / chunkSize) + 1,
                 (changeForms.size() + chunkSize - 1) / chunkSize,
                 numUpserted);
  }
  if (totalUpserted != changeForms.size()) {
    throw std::runtime_error(
      fmt::format("MigrationDatabase: databaseNew took {} of {} change forms",
                  totalUpserted, changeForms.size()));
  }

  spdlog::info("MigrationDatabase: reading databaseNew back to compare");

  std::vector<std::string> missing, extra, differ;
  std::set<std::string> seen;
  newDatabase->Iterate(
    [&](const MpChangeForm& changeForm) {
      auto desc = changeForm.formDesc.ToString();
      seen.insert(desc);
      auto it = oldForms.find(desc);
      if (it == oldForms.end()) {
        extra.push_back(desc);
      } else if (it->second != Dump(changeForm)) {
        differ.push_back(desc);
      }
    },
    std::nullopt);
  for (const auto& [desc, dump] : oldForms) {
    if (!seen.count(desc)) {
      missing.push_back(desc);
    }
  }

  if (!missing.empty() || !extra.empty() || !differ.empty()) {
    throw std::runtime_error(fmt::format(
      "MigrationDatabase: databaseNew does not match the old world: {} "
      "missing ({}), {} extra ({}), {} different ({})",
      missing.size(), FirstFew(missing), extra.size(), FirstFew(extra),
      differ.size(), FirstFew(differ)));
  }

  spdlog::info("MigrationDatabase: {} changeForms moved, and each read back "
               "the same from databaseNew",
               totalUpserted);

  spdlog::info("The server will exit");
  pImpl->exit();
}

std::vector<std::optional<MpChangeForm>>&& MigrationDatabase::UpsertImpl(
  std::vector<std::optional<MpChangeForm>>&& changeForms,
  size_t& outNumUpserted)
{
  spdlog::error("MigrationDatabase::Upsert - should never be reached");
  pImpl->terminate();

  outNumUpserted = 0;
  return std::move(changeForms);
}

void MigrationDatabase::Iterate(const IterateCallback&,
                                std::optional<std::vector<FormDesc>>)
{
  spdlog::error("MigrationDatabase::Iterate - should never be reached");
  pImpl->terminate();
}
