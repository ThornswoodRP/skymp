#pragma once
#include "MpChangeForms.h"
#include "utils/JsonSanitizer.h"
#include "utils/StableIds.h"
#include <database_drivers/IDatabase.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <simdjson.h>

class MongoDatabase
  : public Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  // Connects and makes the indexes the saves and the tools read by
  // (MongoDatabase.cpp says which and why). Throws when it cannot. The ids
  // inside each form are stored by plugin through stableIds, which is the
  // server's load order (utils/StableIds.h).
  MongoDatabase(const std::string& uri_, const std::string& name_,
                std::shared_ptr<const StableIds> stableIds_);

  // IDatabase
  void Iterate(const IterateCallback& iterateCallback,
               std::optional<std::vector<FormDesc>> filter) override;

  // A save is acknowledged only once it is in MongoDB's journal.
  bool SupportsDurableUpsert() const noexcept override;

  struct IndexInfo
  {
    std::string name;
    std::string keyJson;
    bool unique = false;
  };

  // The indexes on the change form collection, as MongoDB lists them.
  std::vector<IndexInfo> ListIndexes();

private:
  std::vector<std::optional<MpChangeForm>>&& UpsertImpl(
    std::vector<std::optional<MpChangeForm>>&& changeForms,
    size_t& outNumUpserted) override;

  struct Impl;
  std::shared_ptr<Impl> pImpl;
};
