// The MongoDB driver against a real mongod (Thornswood #6237).
//
// These run when SKYMP_TEST_MONGO_URI names a mongod (Thornswood PR Linux
// starts one beside the tests and sets it), and SKIP when it is not set, as
// on a machine with no mongod. Each test uses a database of its own, made from
// a random name, and drops it at the end.
#include "MpChangeForms.h"
#include "database_drivers/FileDatabase.h"
#include "database_drivers/MigrationDatabase.h"
#include "database_drivers/MongoDatabase.h"
#include "database_drivers/utils/StableIds.h"
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <spdlog/spdlog.h>

#ifndef NO_MONGO
#  include <bsoncxx/builder/basic/document.hpp>
#  include <bsoncxx/builder/basic/kvp.hpp>
#  include <bsoncxx/json.hpp>
#  include <mongocxx/client.hpp>
#  include <mongocxx/instance.hpp>
#  include <mongocxx/uri.hpp>
#endif

namespace {
using Database =
  Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>;

const char* MongoUri()
{
  const char* uri = std::getenv("SKYMP_TEST_MONGO_URI");
  return (uri && *uri) ? uri : nullptr;
}

std::string RandomName(const char* prefix)
{
  static std::mt19937_64 rng{ std::random_device{}() };
  return std::string(prefix) + std::to_string(rng() % 1000000000000ull);
}

MpChangeForm Form(const char* desc, int32_t profileId = -1, float x = 0)
{
  MpChangeForm res;
  res.formDesc = FormDesc::FromString(desc);
  res.profileId = profileId;
  res.position = { x, 2.5f, -3.75f };
  return res;
}

std::vector<MpChangeForm> All(Database& db)
{
  std::vector<MpChangeForm> res;
  db.Iterate([&](const MpChangeForm& f) { res.push_back(f); }, std::nullopt);
  return res;
}

std::string Dump(const MpChangeForm& f)
{
  return MpChangeForm::ToJson(f).dump();
}

#ifndef NO_MONGO
struct ScopedMongoDatabase
{
  explicit ScopedMongoDatabase(const char* prefix)
    : name(RandomName(prefix))
  {
  }
  ~ScopedMongoDatabase()
  {
    try {
      mongocxx::instance::current();
      mongocxx::client client{ mongocxx::uri{ MongoUri() } };
      client[name].drop();
    } catch (...) {
    }
  }
  std::shared_ptr<MongoDatabase> Open(
    const espm::LoadOrder& order =
      espm::LoadOrder::FullPlugins({ "Skyrim.esm" })) const
  {
    return std::make_shared<MongoDatabase>(
      MongoUri(), name,
      std::make_shared<const StableIds>(order, std::vector<std::string>{}));
  }
  const std::string name;
};
#endif
}

#define SKIP_WITHOUT_MONGO()                                                  \
  if (!MongoUri()) {                                                          \
    SKIP("SKYMP_TEST_MONGO_URI is not set, so no mongod to test against");    \
  }

#ifndef NO_MONGO

TEST_CASE("MongoDB driver makes the formDesc and profileId indexes",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_index_");
  auto db = scoped.Open();

  std::map<std::string, MongoDatabase::IndexInfo> byName;
  for (auto& index : db->ListIndexes()) {
    byName[index.name] = index;
  }
  REQUIRE(byName.count("formDesc_unique"));
  REQUIRE(byName["formDesc_unique"].unique);
  REQUIRE(byName["formDesc_unique"].keyJson.find("\"formDesc\"") !=
          std::string::npos);
  REQUIRE(byName.count("profileId"));
  REQUIRE(!byName["profileId"].unique);

  // A second start finds them there and makes nothing new.
  auto again = scoped.Open();
  REQUIRE(again->ListIndexes().size() == byName.size());
  REQUIRE(db->SupportsDurableUpsert());
}

TEST_CASE("MongoDB driver keeps a form whole, the way the file driver does",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_whole_");
  auto db = scoped.Open();

  auto named = Form("1", 7);
  named.displayName = "Brynja";
  named.setNodeScale = std::map<std::string, float>{ { "NPC Head", 1.25f } };
  REQUIRE(db->Upsert({ named }) == 1);
  REQUIRE(Dump(All(*db).at(0)) == Dump(named));

  // The optional fields go away: a $set would have kept the old ones.
  auto plain = Form("1", 7);
  REQUIRE(db->Upsert({ plain }) == 1);
  auto back = All(*db);
  REQUIRE(back.size() == 1);
  REQUIRE(!back[0].displayName.has_value());
  REQUIRE(!back[0].setNodeScale.has_value());
  REQUIRE(Dump(back[0]) == Dump(plain));
}

TEST_CASE(
  "MongoDB driver takes the last copy of a form named twice in a batch",
  "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_twice_");
  auto db = scoped.Open();

  REQUIRE(db->Upsert({ Form("5", -1, 1), Form("6"), Form("5", -1, 9) }) == 3);
  auto back = All(*db);
  REQUIRE(back.size() == 2);
  REQUIRE(back[0].formDesc.ToString() == "5");
  REQUIRE(back[0].position[0] == 9);
}

TEST_CASE("MongoDB driver loads in formDesc order, filtered or not",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_order_");
  auto db = scoped.Open();

  std::vector<std::optional<MpChangeForm>> batch;
  std::set<std::string> expected;
  for (int i = 0; i < 2500; ++i) {
    char desc[32];
    snprintf(desc, sizeof desc, "%x:Skyrim.esm", 0x10000 + i * 7);
    batch.push_back(Form(desc, i % 3 == 0 ? i : -1, float(i)));
    expected.insert(FormDesc::FromString(desc).ToString());
  }
  REQUIRE(db->Upsert(std::move(batch)) == 2500);

  std::vector<std::string> seen;
  for (auto& f : All(*db)) {
    seen.push_back(f.formDesc.ToString());
  }
  REQUIRE(seen.size() == 2500);
  REQUIRE(std::is_sorted(seen.begin(), seen.end()));
  REQUIRE(std::set<std::string>(seen.begin(), seen.end()) == expected);

  std::vector<FormDesc> filter{ FormDesc::FromString("10007:Skyrim.esm"),
                                FormDesc::FromString("10000:Skyrim.esm"),
                                FormDesc::FromString("ffffff:Skyrim.esm") };
  std::vector<std::string> filtered;
  db->Iterate(
    [&](const MpChangeForm& f) { filtered.push_back(f.formDesc.ToString()); },
    filter);
  REQUIRE(filtered ==
          std::vector<std::string>{ "10000:Skyrim.esm", "10007:Skyrim.esm" });
}

TEST_CASE("MongoDB driver refuses to start on a form kept twice",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_dup_");
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;
    mongocxx::instance::current();
    mongocxx::client client{ mongocxx::uri{ MongoUri() } };
    auto collection = client[scoped.name]["changeForms"];
    collection.insert_one(make_document(kvp("formDesc", "1")));
    collection.insert_one(make_document(kvp("formDesc", "1")));
  }
  REQUIRE_THROWS_WITH(scoped.Open(),
                      Catch::Matchers::ContainsSubstring("kept twice"));
}

TEST_CASE("MongoDB driver keeps a character's items through a load order "
          "change",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_order_change_");
  auto before = espm::LoadOrder::FullPlugins({ "Skyrim.esm", "Sword.esp" });
  auto after =
    espm::LoadOrder::FullPlugins({ "Skyrim.esm", "New.esp", "Sword.esp" });

  auto f = Form("14", 42);
  f.inv.AddItem(FormDesc::FromString("801:Sword.esp").ToFormId(before), 1);
  f.inv.AddItem(0xf, 300);
  f.learnedSpells.LearnSpell(
    FormDesc::FromString("805:Sword.esp").ToFormId(before));
  REQUIRE(scoped.Open(before)->Upsert({ f }) == 1);

  auto back = All(*scoped.Open(after));
  REQUIRE(back.size() == 1);
  std::set<std::string> items;
  for (auto& entry : back[0].inv.entries) {
    items.insert(FormDesc::FromFormId(entry.baseId, after).ToString());
  }
  REQUIRE(items == std::set<std::string>{ "801:Sword.esp", "f:Skyrim.esm" });
  REQUIRE(back[0].learnedSpells.GetLearnedSpells() ==
          std::vector<uint32_t>{
            FormDesc::FromString("805:Sword.esp").ToFormId(after) });
}

TEST_CASE("Migration from files to MongoDB moves every form and checks it",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_move_");
  auto dir = std::filesystem::temp_directory_path() / RandomName("tw_files_");
  std::filesystem::remove_all(dir);
  auto files =
    std::make_shared<FileDatabase>(dir.string(), spdlog::default_logger());
  std::vector<std::optional<MpChangeForm>> batch;
  for (int i = 0; i < 1500; ++i) {
    char desc[32];
    snprintf(desc, sizeof desc, "%x:Skyrim.esm", 0x20000 + i);
    auto f = Form(desc, i % 10 == 0 ? 100 + i : -1, float(i) / 3);
    if (i % 10 == 0)
      f.displayName = "Name " + std::to_string(i);
    f.inv.AddItem(0xf, 1 + i);
    batch.push_back(f);
  }
  REQUIRE(files->Upsert(std::move(batch)) == 1500);

  bool exited = false;
  auto mongo = scoped.Open();
  std::make_shared<MigrationDatabase>(mongo, files, [&] { exited = true; });
  REQUIRE(exited);

  auto fromFiles = All(*files);
  auto fromMongo = All(*mongo);
  REQUIRE(fromMongo.size() == 1500);
  std::map<std::string, std::string> a, b;
  for (auto& f : fromFiles)
    a[f.formDesc.ToString()] = Dump(f);
  for (auto& f : fromMongo)
    b[f.formDesc.ToString()] = Dump(f);
  REQUIRE(a == b);

  // The move stores the ids inside each form by plugin: the file world kept
  // numbers.
  {
    mongocxx::client client{ mongocxx::uri{ MongoUri() } };
    auto doc = client[scoped.name]["changeForms"].find_one(
      bsoncxx::builder::basic::make_document(
        bsoncxx::builder::basic::kvp("formDesc", "20000:Skyrim.esm")));
    REQUIRE(doc);
    auto text = bsoncxx::to_json(doc->view());
    REQUIRE_THAT(text, Catch::Matchers::ContainsSubstring("\"f:Skyrim.esm\""));
  }

  // A second run finds MongoDB holding a world and moves nothing.
  exited = false;
  REQUIRE_THROWS_WITH(
    std::make_shared<MigrationDatabase>(mongo, files, [&] { exited = true; }),
    Catch::Matchers::ContainsSubstring("already holds 1500"));
  REQUIRE(!exited);
  std::filesystem::remove_all(dir);
}

TEST_CASE("Migration refuses a file world with a file it cannot read",
          "[MongoDatabaseServer]")
{
  SKIP_WITHOUT_MONGO();
  ScopedMongoDatabase scoped("tw_torn_");
  auto dir = std::filesystem::temp_directory_path() / RandomName("tw_torn_");
  std::filesystem::remove_all(dir);
  auto files =
    std::make_shared<FileDatabase>(dir.string(), spdlog::default_logger());
  REQUIRE(files->Upsert({ Form("1"), Form("2") }) == 2);
  {
    std::ofstream torn(dir / "changeForms" / "3.json");
    torn << "{\"formDesc\": \"3\", \"recType\"";
  }

  bool exited = false;
  auto mongo = scoped.Open();
  REQUIRE_THROWS_WITH(
    std::make_shared<MigrationDatabase>(mongo, files, [&] { exited = true; }),
    Catch::Matchers::ContainsSubstring("could not be read"));
  REQUIRE(!exited);
  REQUIRE(All(*mongo).empty());
  std::filesystem::remove_all(dir);
}

#endif
