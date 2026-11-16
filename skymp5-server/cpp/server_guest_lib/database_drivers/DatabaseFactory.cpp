#include "DatabaseFactory.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "FileDatabase.h"
#include "MigrationDatabase.h"
#include "ZipDatabase.h"
#include <database_drivers/MongoDatabase.h>
#include <fstream>
#include <iterator>

namespace {
std::string ReadUriFile(const std::string& path)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("databaseUriFile '" + path +
                             "' could not be read");
  }
  std::string text((std::istreambuf_iterator<char>(stream)),
                   std::istreambuf_iterator<char>());
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    throw std::runtime_error("databaseUriFile '" + path + "' is empty");
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}
}

std::shared_ptr<Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>>
DatabaseFactory::Create(nlohmann::json settings,
                        std::shared_ptr<spdlog::logger> logger)
{
  auto databaseDriver = settings.count("databaseDriver")
    ? settings["databaseDriver"].get<std::string>()
    : std::string("file");

  if (databaseDriver == "file") {
    auto databaseName = settings.count("databaseName")
      ? settings["databaseName"].get<std::string>()
      : std::string("world");

    logger->info("Using file with name '" + databaseName + "'");
    return std::make_shared<FileDatabase>(databaseName, logger);
  }

  if (databaseDriver == "mongodb") {
    auto databaseName = settings.count("databaseName")
      ? settings["databaseName"].get<std::string>()
      : std::string("db");

    // databaseUriFile names a file holding the connection string, so a
    // password in it stays out of server-settings.json, which backups and
    // tools copy and read (Thornswood #6237). The file's text is never logged.
    std::string databaseUri;
    if (settings.count("databaseUriFile")) {
      databaseUri = ReadUriFile(settings["databaseUriFile"].get<std::string>());
    } else {
      databaseUri = settings.at("databaseUri").get<std::string>();
    }
    logger->info("Using mongodb with name '" + databaseName + "'");
    return std::make_shared<MongoDatabase>(databaseUri, databaseName);
  }

  if (databaseDriver == "migration") {
    auto from = settings.at("databaseOld");
    auto to = settings.at("databaseNew");
    auto oldDatabase = Create(from, logger);
    auto newDatabase = Create(to, logger);
    return std::make_shared<MigrationDatabase>(newDatabase, oldDatabase);
  }

  if (databaseDriver == "zip") {
    auto databaseName = settings.count("databaseName")
      ? settings["databaseName"].get<std::string>()
      : std::string("world");

    logger->info("Using zip with name '" + databaseName + "'");
    return std::make_shared<ZipDatabase>(databaseName, logger);
  }

  throw std::runtime_error("Unrecognized databaseDriver: " + databaseDriver);
}
