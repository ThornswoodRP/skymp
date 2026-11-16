#pragma once
#include "MpChangeForms.h"
#include "libespm/LoadOrder.h"
#include <database_drivers/IDatabase.h>
#include <memory>
#include <spdlog/spdlog.h>

class StableIds;

class DatabaseFactory
{
public:
  using Database =
    Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>;

  // loadOrder is the server's (espm::Loader::GetLoadOrder). The MongoDB
  // driver stores the ids inside each form by plugin through it, and a
  // setting of forgetPlugins names plugins taken out on purpose
  // (utils/StableIds.h).
  static std::shared_ptr<Database> Create(
    nlohmann::json settings, std::shared_ptr<spdlog::logger> logger,
    const espm::LoadOrder& loadOrder);

private:
  static std::shared_ptr<Database> Create(
    nlohmann::json settings, std::shared_ptr<spdlog::logger> logger,
    const std::shared_ptr<const StableIds>& stableIds);
};
