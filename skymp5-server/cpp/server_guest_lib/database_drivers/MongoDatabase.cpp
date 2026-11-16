#include "MongoDatabase.h"

#include "JsonUtils.h"
#include <save_storages/AsyncSaveStorage.h>

#ifndef NO_MONGO
#  include <bsoncxx/builder/basic/document.hpp>
#  include <bsoncxx/builder/basic/kvp.hpp>
#  include <bsoncxx/json.hpp>
#  include <mongocxx/bulk_write.hpp>
#  include <mongocxx/client.hpp>
#  include <mongocxx/exception/exception.hpp>
#  include <mongocxx/hint.hpp>
#  include <mongocxx/instance.hpp>
#  include <mongocxx/model/replace_one.hpp>
#  include <mongocxx/options/bulk_write.hpp>
#  include <mongocxx/options/find.hpp>
#  include <mongocxx/options/index.hpp>
#  include <mongocxx/pool.hpp>
#  include <mongocxx/uri.hpp>
#  include <mongocxx/write_concern.hpp>
#endif

#include <map>
#include <mutex>
#include <set>
#include <sodium.h>
#include <spdlog/spdlog.h>
#include <vector>

// Thornswood #6237: the saves leave the file driver for MongoDB, so this is
// the driver every world is kept in. What changed from upstream, and why:
//
// INDEXES. Upstream made none. Every save is an upsert keyed on formDesc,
// and without an index on it MongoDB finds each one by reading the whole
// collection, so a batch of k forms against a world of n costs k * n reads
// and grows with every form the world keeps. The driver now makes, at every
// start and before the first read:
//   formDesc_unique  {formDesc: 1}, unique. The key every save and every
//                    filtered load looks a form up by. Unique, so one form can
//                    never be kept twice; a collection that already holds a
//                    form twice refuses to start and names it, instead of one
//                    copy being picked at random.
//   profileId        {profileId: 1}. A person's characters (profileId above 0)
//                    and the NPCs (below 0), which is how every staff and box
//                    tool asks (Delete-Person, Reset-Character, the NPC reset,
//                    the bot pass, the backups' character count). A
//                    character's inventory, equipment and quest record are
//                    fields of its own document, so this is also the index
//                    that finds a character's inventory.
// No other index: the engine loads the whole world into memory at start and
// never asks the database anything else, and an index nobody reads only
// makes every save slower.
//
// WHOLE-DOCUMENT WRITES. Upstream wrote each form with $set of its fields,
// which never removes a field. A form whose optional field went away (a
// display name, a node scale, pickup extra data) kept the old value in the
// database and got it back at the next start. The file driver rewrites the
// whole file, so this is a replace, the same as a file.
//
// JOURNALED WRITES. A save is acknowledged only once it is in MongoDB's
// journal on disk (j: true), and a batch whose acknowledged count is not
// the number of forms in it fails. So a successful Upsert is durable, and
// SupportsDurableUpsert says so.
//
// ONE ORDERED LOAD THAT STOPS ON AN ERROR. Upstream split the load over up to
// 100 threads with skip and limit and no sort. MongoDB keeps no order
// between separate queries without a sort, so two parts could share a form
// and miss another, and the count check at the end passes when the two
// cancel out. And a part that failed was retried in a loop with no end, so a
// database that stayed down held the server at start for ever, saying only
// "Retrying". Now one cursor walks the collection in formDesc order over the
// unique index; a form seen twice, a count that does not match, or any error
// stops the load with what went wrong.
//
// IDS BY PLUGIN. The ids inside a form (its items, spells, head parts, race
// and effects) are stored as "id:Plugin", the way its own keys are, and read
// back as the ids this load order gives them, so a load order change cannot
// shift them (utils/StableIds.h). A document saved with numbers reads as it
// always did, and its next save stores descriptors.

#ifdef NO_MONGO
struct MongoDatabase::Impl
{
};

MongoDatabase::MongoDatabase(const std::string&, const std::string&,
                             std::shared_ptr<const StableIds>)
{
  throw std::runtime_error("MongoDB is not supported in this build");
}

std::vector<std::optional<MpChangeForm>>&& MongoDatabase::UpsertImpl(
  std::vector<std::optional<MpChangeForm>>&& changeForms,
  size_t& outNumUpserted)
{
  outNumUpserted = 0;
  return std::move(changeForms);
}

void MongoDatabase::Iterate(const IterateCallback&,
                            std::optional<std::vector<FormDesc>>)
{
}

bool MongoDatabase::SupportsDurableUpsert() const noexcept
{
  return false;
}

std::vector<MongoDatabase::IndexInfo> MongoDatabase::ListIndexes()
{
  return {};
}
#endif

#ifndef NO_MONGO
namespace {
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

constexpr const char* kCollectionName = "changeForms";

std::string BytesToHexString(const uint8_t* bytes, size_t length)
{
  static constexpr auto kHexDigits = "0123456789abcdef";

  std::string hexStr(length * 2, ' ');
  for (size_t i = 0; i < length; ++i) {
    hexStr[2 * i] = kHexDigits[(bytes[i] >> 4) & 0xF];
    hexStr[2 * i + 1] = kHexDigits[bytes[i] & 0xF];
  }

  return hexStr;
}

std::string Sha256(const std::string& str)
{
  unsigned char hash[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(hash, reinterpret_cast<const unsigned char*>(str.data()),
                     str.size());
  return BytesToHexString(hash, crypto_hash_sha256_BYTES);
}

mongocxx::write_concern JournaledWriteConcern()
{
  mongocxx::write_concern concern;
  concern.acknowledge_level(mongocxx::write_concern::level::k_acknowledged);
  concern.journal(true);
  return concern;
}
}

struct MongoDatabase::Impl
{
  const std::string uri;
  const std::string name;

  std::shared_ptr<mongocxx::pool> pool;

  std::unique_ptr<JsonSanitizer> jsonSanitizer;

  std::shared_ptr<const StableIds> stableIds;

  // What StableIds noted, each said once: a form saved every few seconds
  // with an id no plugin owns would otherwise say it at every save.
  std::mutex notesMutex;
  std::set<std::string> notesSaid;

  void Say(const StableIds::Notes& notes)
  {
    if (notes.empty()) {
      return;
    }
    std::lock_guard lock(notesMutex);
    for (const auto& note : notes) {
      if (notesSaid.insert(note).second) {
        spdlog::warn("MongoDB: {}", note);
      }
    }
  }

  void EnsureIndexes()
  {
    auto entry = pool->acquire();
    auto collection = entry->database(name).collection(kCollectionName);

    mongocxx::options::index formDescOptions;
    formDescOptions.name("formDesc_unique");
    formDescOptions.unique(true);

    mongocxx::options::index profileIdOptions;
    profileIdOptions.name("profileId");

    try {
      collection.create_index(make_document(kvp("formDesc", 1)),
                              formDescOptions);
    } catch (const mongocxx::exception& e) {
      // 11000 is a duplicate key: the collection already keeps one form in
      // two documents, so which one is the form is not known here.
      throw std::runtime_error(
        fmt::format("MongoDB: the formDesc index could not be made on '{}.{}' "
                    "({}). If it names a duplicate key, that form is kept "
                    "twice; nothing was changed, and the server does not "
                    "start until only one copy is left",
                    name, kCollectionName, e.what()));
    }

    try {
      collection.create_index(make_document(kvp("profileId", 1)),
                              profileIdOptions);
    } catch (const mongocxx::exception& e) {
      throw std::runtime_error(
        fmt::format("MongoDB: the profileId index could not be made on "
                    "'{}.{}' ({})",
                    name, kCollectionName, e.what()));
    }
  }
};

MongoDatabase::MongoDatabase(const std::string& uri_, const std::string& name_,
                             std::shared_ptr<const StableIds> stableIds_)
{
  if (!stableIds_) {
    throw std::runtime_error("MongoDB: the driver needs the server's load "
                             "order to store ids by plugin");
  }
  // One driver instance per process, shared with anything else that
  // talks to MongoDB here (the tests do).
  mongocxx::instance::current();

  // Since MongoDB 5.0, '$' and '.' are OK
  // Might be de-hardcoded in the future, for DocumentDB or something
  static const std::vector<char> kBannedCharactersMongo5 = { '\0' };

  pImpl.reset(new Impl{ uri_, name_ });
  pImpl->stableIds = std::move(stableIds_);
  pImpl->pool.reset(new mongocxx::pool(mongocxx::uri(pImpl->uri.data())));
  pImpl->jsonSanitizer.reset(
    new JsonSanitizer(kBannedCharactersMongo5,
                      [](const std::string& str) { return Sha256(str); }));

  pImpl->EnsureIndexes();
}

bool MongoDatabase::SupportsDurableUpsert() const noexcept
{
  return true;
}

std::vector<MongoDatabase::IndexInfo> MongoDatabase::ListIndexes()
{
  auto entry = pImpl->pool->acquire();
  auto collection = entry->database(pImpl->name).collection(kCollectionName);
  std::vector<IndexInfo> res;
  for (auto&& index : collection.list_indexes()) {
    IndexInfo info;
    info.name = std::string(index["name"].get_string().value);
    info.keyJson = bsoncxx::to_json(index["key"].get_document().value);
    auto unique = index["unique"];
    info.unique = unique && unique.get_bool().value;
    res.push_back(std::move(info));
  }
  return res;
}

std::vector<std::optional<MpChangeForm>>&& MongoDatabase::UpsertImpl(
  std::vector<std::optional<MpChangeForm>>&& changeForms,
  size_t& outNumUpserted)
{
  outNumUpserted = 0;
  try {
    // One write per form: a batch may name a form twice, and the last copy
    // is the one kept, the same as the file driver.
    std::map<std::string, size_t> lastPosition;
    for (size_t i = 0; i < changeForms.size(); ++i) {
      if (changeForms[i]) {
        lastPosition[changeForms[i]->formDesc.ToString()] = i;
      }
    }
    if (lastPosition.empty()) {
      return std::move(changeForms);
    }

    auto poolEntry = pImpl->pool->acquire();
    auto collection =
      poolEntry->database(pImpl->name).collection(kCollectionName);

    mongocxx::options::bulk_write options;
    options.ordered(true);
    options.write_concern(JournaledWriteConcern());
    auto bulk = collection.create_bulk_write(options);

    size_t submitted = 0;
    for (const auto& [formDesc, position] : lastPosition) {
      const auto& changeForm = *changeForms[position];
      auto json = MpChangeForm::ToJson(changeForm);
      StableIds::Notes notes;
      pImpl->stableIds->Encode(json, notes);
      pImpl->Say(notes);
      auto document = pImpl->jsonSanitizer->SanitizeJsonRecursive(json);
      mongocxx::model::replace_one replace(
        make_document(kvp("formDesc", formDesc)),
        bsoncxx::from_json(document.dump()));
      replace.upsert(true);
      bulk.append(replace);
      ++submitted;
    }

    auto result = bulk.execute();
    if (!result) {
      throw std::runtime_error("MongoDB did not acknowledge the save batch");
    }
    const auto written = static_cast<size_t>(result->matched_count()) +
      static_cast<size_t>(result->upserted_count());
    if (written != submitted) {
      throw std::runtime_error(fmt::format(
        "MongoDB acknowledged {} of the {} forms in the save batch "
        "({} matched, {} inserted)",
        written, submitted, result->matched_count(),
        result->upserted_count()));
    }

    // Counted as the file driver counts: every form submitted, a form named
    // twice in the batch included.
    size_t numForms = 0;
    for (const auto& changeForm : changeForms) {
      if (changeForm) {
        ++numForms;
      }
    }
    outNumUpserted = numForms;
    return std::move(changeForms);
  } catch (std::exception& e) {
    throw Viet::AsyncSaveStorage<
      MpChangeForm, FormDesc,
      std::vector<FormDesc>>::UpsertFailedException(std::move(changeForms),
                                                    e.what());
  }
}

void MongoDatabase::Iterate(const IterateCallback& iterateCallback,
                            std::optional<std::vector<FormDesc>> filter)
{
  try {
    nlohmann::json filterJson = nlohmann::json::object();
    if (filter) {
      auto filterArr = nlohmann::json::array();
      for (const auto& desc : *filter) {
        filterArr.push_back(desc.ToString());
      }
      filterJson["formDesc"] = { { "$in", std::move(filterArr) } };
      spdlog::info("MongoDB: loading {} named forms", filter->size());
    } else {
      spdlog::info("MongoDB: loading every form");
    }
    const auto filterBson = bsoncxx::from_json(filterJson.dump());

    auto poolEntry = pImpl->pool->acquire();
    auto collection =
      poolEntry->database(pImpl->name).collection(kCollectionName);

    const auto expected =
      static_cast<size_t>(collection.count_documents(filterBson.view()));

    mongocxx::options::find options;
    options.batch_size(1000);
    options.sort(make_document(kvp("formDesc", 1)));
    options.hint(mongocxx::hint("formDesc_unique"));

    simdjson::dom::parser parser;
    simdjson::dom::parser restoredParser;
    std::string hash;
    std::string previous;
    size_t processed = 0;
    size_t dropped = 0;

    auto cursor = collection.find(filterBson.view(), options);
    for (auto&& documentView : cursor) {
      const std::string documentJson = bsoncxx::to_json(documentView);
      auto document = parser.parse(documentJson).value();

      bool restored = false;
      nlohmann::json restoredDocument =
        pImpl->jsonSanitizer->RestoreSanitizedJsonRecursive(document,
                                                            restored);

      StableIds::Notes notes;
      pImpl->stableIds->Decode(restoredDocument, notes);
      pImpl->Say(notes);
      dropped += notes.size();

      const std::string decodedDump = restoredDocument.dump();
      auto decoded = restoredParser.parse(decodedDump).value();
      MpChangeFormREFR changeForm = MpChangeForm::JsonToChangeForm(decoded);

      auto desc = changeForm.formDesc.ToString();
      if (processed > 0 && !(previous < desc)) {
        throw std::runtime_error(fmt::format(
          "MongoDB returned form '{}' after '{}', out of formDesc order or "
          "twice",
          desc, previous));
      }
      previous = desc;

      iterateCallback(changeForm);

      ++processed;
      hash = Sha256(hash + desc);
    }

    // The same each time for the same world: the order is formDesc's.
    spdlog::info("MongoDB: hash of the form order {}", hash);

    if (processed != expected) {
      throw std::runtime_error(
        fmt::format("MongoDB: loaded {} forms where the collection counted {}",
                    processed, expected));
    }
    spdlog::info("MongoDB: loaded {} forms", processed);
    if (dropped > 0) {
      spdlog::warn("MongoDB: {} thing(s) from forgotten plugins were dropped "
                   "from the forms, each said above",
                   dropped);
    }
  } catch (std::exception& e) {
    throw Viet::AsyncSaveStorage<
      MpChangeForm, FormDesc,
      std::vector<FormDesc>>::IterateFailedException(std::move(filter),
                                                     e.what());
  }
}

#endif // #ifndef NO_MONGO
