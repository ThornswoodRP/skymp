#include "LoadTrace.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// THORNSWOOD PATCH (#6200): LoadTrace.h says what this measures and why.
namespace {
using Clock = std::chrono::steady_clock;

constexpr auto kSampleEvery = std::chrono::milliseconds(10);
constexpr auto kLineEvery = std::chrono::milliseconds(1000);
constexpr size_t kTopModules = 12;
constexpr size_t kTopGamePlaces = 10;
constexpr size_t kTopFiles = 15;
constexpr uint32_t kDataLoaded = 8;

struct Totals
{
  uint64_t mainCpuMs = 0;
  uint64_t gameCpuMs = 0;
  uint64_t readBytes = 0;
  uint64_t readOps = 0;
};

struct Phase
{
  std::string name;
  Clock::time_point began;
  Totals atStart;
  std::map<std::string, uint32_t> modules;
  std::map<uint64_t, uint32_t> gamePlaces;
  uint32_t samples = 0;
  uint32_t readingFiles = 0;
};

HANDLE g_mainThread = nullptr;
HMODULE g_game = nullptr;
Clock::time_point g_started;
std::atomic<bool> g_dataLoaded = false;
std::mutex g_mutex;
std::vector<Phase> g_phases;

uint64_t Ms(const FILETIME& t)
{
  ULARGE_INTEGER v;
  v.LowPart = t.dwLowDateTime;
  v.HighPart = t.dwHighDateTime;
  return v.QuadPart / 10000;
}

Totals Now()
{
  Totals t;
  FILETIME created, exited, kernel, user;
  if (GetThreadTimes(g_mainThread, &created, &exited, &kernel, &user)) {
    t.mainCpuMs = Ms(kernel) + Ms(user);
  }
  if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel,
                      &user)) {
    t.gameCpuMs = Ms(kernel) + Ms(user);
  }
  IO_COUNTERS io{};
  if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
    t.readBytes = io.ReadTransferCount;
    t.readOps = io.ReadOperationCount;
  }
  return t;
}

long long Since(Clock::time_point from, Clock::time_point to)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(to - from)
    .count();
}

const char* MessageName(uint32_t type)
{
  switch (type) {
    case 0:
      return "post load";
    case 1:
      return "post post load";
    case 2:
      return "pre load game";
    case 3:
      return "post load game";
    case 4:
      return "save game";
    case 5:
      return "delete game";
    case 6:
      return "input loaded";
    case 7:
      return "new game";
    case 8:
      return "data loaded";
  }
  return "other";
}

// Where the main thread is: its instruction pointer, read while it is
// stopped for that one read. Nothing that can take a lock runs while it is
// stopped, so it cannot be left waiting on this thread.
bool MainThreadAt(uint64_t& rip)
{
  if (SuspendThread(g_mainThread) == static_cast<DWORD>(-1)) {
    return false;
  }
  CONTEXT context{};
  context.ContextFlags = CONTEXT_CONTROL;
  const bool read = GetThreadContext(g_mainThread, &context) != 0;
  ResumeThread(g_mainThread);
  rip = read ? context.Rip : 0;
  return read;
}

std::string ModuleOf(uint64_t rip, HMODULE& module)
{
  static std::unordered_map<HMODULE, std::string> names;
  module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(rip), &module)) {
    return "outside any module";
  }
  auto found = names.find(module);
  if (found != names.end()) {
    return found->second;
  }
  char path[MAX_PATH] = {};
  GetModuleFileNameA(module, path, MAX_PATH);
  const char* slash = std::strrchr(path, '\\');
  std::string name = slash ? slash + 1 : path;
  names[module] = name;
  return name;
}

// The plugin file the game is reading now, or "" when it reads none.
std::string FileBeingRead(bool& reading)
{
  reading = false;
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!dataHandler) {
    return "";
  }
  reading = dataHandler->GetGeometryRuntimeData().loadingFiles;
  const RE::TESFile* file = dataHandler->activeFile;
  if (!file) {
    return "";
  }
  return std::string(file->fileName,
                     strnlen(file->fileName, sizeof(file->fileName)));
}

template <class Map>
std::vector<std::pair<typename Map::key_type, uint32_t>> Top(const Map& map,
                                                             size_t most)
{
  std::vector<std::pair<typename Map::key_type, uint32_t>> rows(map.begin(),
                                                                map.end());
  std::sort(rows.begin(), rows.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  if (rows.size() > most) {
    rows.resize(most);
  }
  return rows;
}

uint32_t Percent(uint32_t part, uint32_t whole)
{
  return whole ? part * 100 / whole : 0;
}

void Report(Clock::time_point ended, const Totals& atEnd,
            const std::map<std::string, long long>& fileMs)
{
  std::lock_guard lock(g_mutex);
  logger::info("[load] from the plugin's load to the game's data load: {} ms",
               Since(g_started, ended));
  for (size_t i = 0; i < g_phases.size(); ++i) {
    const Phase& phase = g_phases[i];
    const bool last = i + 1 == g_phases.size();
    const auto to = last ? ended : g_phases[i + 1].began;
    const Totals& end = last ? atEnd : g_phases[i + 1].atStart;
    const long long wall = Since(phase.began, to);
    logger::info(
      "[load] {}: {} ms, the main thread used {} ms of processor ({}%), the "
      "game {} ms over all its threads, read {:.1f} MB in {} reads; {} "
      "looks, {}% of them while the game was reading plugin files",
      phase.name, wall, end.mainCpuMs - phase.atStart.mainCpuMs,
      wall > 0 ? (end.mainCpuMs - phase.atStart.mainCpuMs) * 100 /
          static_cast<uint64_t>(wall)
               : 0,
      end.gameCpuMs - phase.atStart.gameCpuMs,
      (end.readBytes - phase.atStart.readBytes) / (1024.0 * 1024.0),
      end.readOps - phase.atStart.readOps, phase.samples,
      Percent(phase.readingFiles, phase.samples));
    for (const auto& [name, count] : Top(phase.modules, kTopModules)) {
      logger::info("[load]   main thread in {}: {}%", name,
                   Percent(count, phase.samples));
    }
    for (const auto& [page, count] : Top(phase.gamePlaces, kTopGamePlaces)) {
      logger::info("[load]   in SkyrimSE.exe at +0x{:X}: {}%", page << 12,
                   Percent(count, phase.samples));
    }
  }
  std::vector<std::pair<std::string, long long>> files(fileMs.begin(),
                                                       fileMs.end());
  long long all = 0;
  for (const auto& file : files) {
    all += file.second;
  }
  std::sort(files.begin(), files.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  logger::info("[load] {} plugin files read in {} ms in all; the slowest:",
               files.size(), all);
  for (size_t i = 0; i < files.size() && i < kTopFiles; ++i) {
    logger::info("[load]   {}: {} ms", files[i].first, files[i].second);
  }
}

void Sample()
{
  std::map<std::string, long long> fileMs;
  std::string file;
  auto fileSince = Clock::now();
  auto lineSince = Clock::now();
  Totals lineAt = Now();
  std::map<std::string, uint32_t> lineModules;

  while (!g_dataLoaded) {
    std::this_thread::sleep_for(kSampleEvery);
    const auto now = Clock::now();

    uint64_t rip = 0;
    std::string module = "not readable";
    uint64_t gamePlace = 0;
    bool inGame = false;
    if (MainThreadAt(rip)) {
      HMODULE handle = nullptr;
      module = ModuleOf(rip, handle);
      if (handle && handle == g_game) {
        inGame = true;
        gamePlace = (rip - reinterpret_cast<uint64_t>(g_game)) >> 12;
      }
    }

    bool reading = false;
    const std::string reads = FileBeingRead(reading);
    if (reads != file) {
      if (!file.empty()) {
        fileMs[file] += Since(fileSince, now);
      }
      file = reads;
      fileSince = now;
    }

    {
      std::lock_guard lock(g_mutex);
      if (!g_phases.empty()) {
        Phase& phase = g_phases.back();
        ++phase.samples;
        ++phase.modules[module];
        if (inGame) {
          ++phase.gamePlaces[gamePlace];
        }
        if (reading) {
          ++phase.readingFiles;
        }
      }
    }
    ++lineModules[module];

    if (now - lineSince >= kLineEvery) {
      const Totals at = Now();
      const auto mostly = Top(lineModules, 1);
      logger::info("[load] +{} ms: the main thread used {} ms of processor, "
                   "the game {} ms, read {:.1f} MB in {} reads, reading {}, "
                   "the main thread mostly in {}",
                   Since(g_started, now), at.mainCpuMs - lineAt.mainCpuMs,
                   at.gameCpuMs - lineAt.gameCpuMs,
                   (at.readBytes - lineAt.readBytes) / (1024.0 * 1024.0),
                   at.readOps - lineAt.readOps,
                   file.empty() ? std::string("no plugin file") : file,
                   mostly.empty() ? std::string("nothing") : mostly[0].first);
      lineAt = at;
      lineSince = now;
      lineModules.clear();
    }
  }

  const auto ended = Clock::now();
  if (!file.empty()) {
    fileMs[file] += Since(fileSince, ended);
  }
  Report(ended, Now(), fileMs);
}
}

void LoadTrace::Start()
{
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                       GetCurrentProcess(), &g_mainThread,
                       THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                         THREAD_QUERY_INFORMATION,
                       FALSE, 0)) {
    logger::info("[load] the main thread could not be opened ({}), so the "
                 "start is not traced",
                 GetLastError());
    return;
  }
  g_game = GetModuleHandleW(nullptr);
  // Its address is looked up here, on the main thread, before the other
  // thread reads it.
  RE::TESDataHandler::GetSingleton();
  g_started = Clock::now();
  {
    std::lock_guard lock(g_mutex);
    g_phases.push_back({ "from the plugin's load", g_started, Now() });
  }
  std::thread(Sample).detach();
  logger::info("[load] tracing the start every {} ms until the data load",
               kSampleEvery.count());
}

void LoadTrace::Mark(uint32_t skseMessageType)
{
  if (!g_mainThread || g_dataLoaded) {
    return;
  }
  const auto now = Clock::now();
  logger::info("[load] SKSE message {} ({}) at +{} ms", skseMessageType,
               MessageName(skseMessageType), Since(g_started, now));
  if (skseMessageType == kDataLoaded) {
    g_dataLoaded = true;
    return;
  }
  std::lock_guard lock(g_mutex);
  g_phases.push_back(
    { std::string("after ") + MessageName(skseMessageType), now, Now() });
}
