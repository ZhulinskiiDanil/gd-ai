#include "ProgressStore.hpp"
#include "levelKey.hpp"

#include <algorithm>
#include <ctime>

using namespace geode::prelude;

static constexpr size_t MAX_SESSIONS_PER_LEVEL = 20;
static constexpr size_t MAX_LEVELS = 100;
static constexpr size_t SAVED_TOP = 3;
static constexpr size_t JSON_SESSIONS = 10;
static constexpr int64_t DAY = 24 * 60 * 60;

static bool isSavingEnabled()
{
  return Mod::get()->getSettingValue<bool>("save-progress");
}

static std::filesystem::path filePath()
{
  return Mod::get()->getSaveDir() / "progress.json";
}

// key -> {"lastPlayed": unix seconds, "sessions": [...]}
static matjson::Value &data()
{
  static matjson::Value s_data = []
  {
    auto json = file::readJson(filePath()).unwrapOr(matjson::Value());
    return json.isObject() ? json : matjson::makeObject({});
  }();
  return s_data;
}

static void write()
{
  if (auto result = file::writeStringSafe(filePath(), data().dump(matjson::NO_INDENTATION)); result.isErr())
    log::error("Failed to save progress: {}", result.unwrapErr());
}

// A copy of a JSON array, empty if it is not one
static std::vector<matjson::Value> arrayOf(matjson::Value const &json)
{
  if (auto items = json.asArray())
    return items.unwrap();
  return {};
}

static matjson::Value recordToJson(LevelSession const &session)
{
  auto deaths = matjson::Value::array();
  for (auto [percent, count] : session.topDeaths(SAVED_TOP))
    deaths.push(matjson::makeObject({{"percent", percent}, {"deaths", count}}));

  auto runs = matjson::Value::array();
  for (auto [run, count] : session.topRuns(SAVED_TOP))
    runs.push(matjson::makeObject({{"from", run.first}, {"to", run.second}, {"count", count}}));

  return matjson::makeObject({
      {"startedAt", session.startedAt},
      {"attempts", session.attempts},
      {"bestPercent", session.bestPercent},
      {"practice", session.practice},
      {"startPos", session.startPos},
      {"topDeaths", deaths},
      {"topRuns", runs},
  });
}

static SessionRecord recordFromJson(matjson::Value const &json)
{
  SessionRecord record{
      .startedAt = json["startedAt"].as<int64_t>().unwrapOr(0),
      .attempts = json["attempts"].as<int>().unwrapOr(0),
      .bestPercent = json["bestPercent"].as<int>().unwrapOr(0),
      .practice = json["practice"].asBool().unwrapOr(false),
      .startPos = json["startPos"].asBool().unwrapOr(false),
  };

  for (auto const &death : arrayOf(json["topDeaths"]))
    record.topDeaths.push_back({death["percent"].as<int>().unwrapOr(0), death["deaths"].as<int>().unwrapOr(0)});

  for (auto const &run : arrayOf(json["topRuns"]))
    record.topRuns.push_back({{run["from"].as<int>().unwrapOr(0), run["to"].as<int>().unwrapOr(0)},
                              run["count"].as<int>().unwrapOr(0)});

  return record;
}

// Forgets the levels not played for the longest time
static void pruneLevels()
{
  auto &levels = data();
  while (levels.size() > MAX_LEVELS)
  {
    auto oldest = std::min_element(levels.begin(), levels.end(), [](auto const &a, auto const &b)
                                   { return a["lastPlayed"].template as<int64_t>().unwrapOr(0) <
                                            b["lastPlayed"].template as<int64_t>().unwrapOr(0); });
    levels.erase(oldest->getKey().value_or(""));
  }
}

void ProgressStore::record(LevelSession const &session)
{
  if (!isSavingEnabled() || session.attempts <= 0)
    return;

  auto key = levelKey(session.levelId, session.levelName);
  auto &levels = data();
  if (!levels.contains(key))
    levels[key] = matjson::makeObject({{"sessions", matjson::Value::array()}});

  auto &level = levels[key];
  auto sessions = arrayOf(level["sessions"]);
  sessions.push_back(recordToJson(session));
  if (sessions.size() > MAX_SESSIONS_PER_LEVEL)
    sessions.erase(sessions.begin(), sessions.end() - MAX_SESSIONS_PER_LEVEL);

  level["sessions"] = matjson::Value(sessions);
  level["lastPlayed"] = session.startedAt;

  pruneLevels();
  write();
}

std::vector<SessionRecord> ProgressStore::history(std::string const &key, int64_t exceptStartedAt)
{
  std::vector<SessionRecord> records;
  if (!isSavingEnabled() || !data().contains(key))
    return records;

  for (auto const &json : arrayOf(data()[key]["sessions"]))
  {
    auto record = recordFromJson(json);
    if (record.startedAt != exceptStartedAt || exceptStartedAt == 0)
      records.push_back(std::move(record));
  }

  return records;
}

std::optional<matjson::Value> ProgressStore::toJson(std::string const &key, int64_t exceptStartedAt)
{
  auto records = history(key, exceptStartedAt);
  if (records.empty())
    return std::nullopt;

  if (records.size() > JSON_SESSIONS)
    records.erase(records.begin(), records.end() - JSON_SESSIONS);

  auto now = std::time(nullptr);
  auto sessions = matjson::Value::array();

  for (auto const &record : records)
  {
    auto deaths = matjson::Value::array();
    for (auto [percent, count] : record.topDeaths)
      deaths.push(matjson::makeObject({{"percent", percent}, {"deaths", count}}));

    auto runs = matjson::Value::array();
    for (auto [run, count] : record.topRuns)
      runs.push(matjson::makeObject({{"from", run.first}, {"to", run.second}, {"count", count}}));

    sessions.push(matjson::makeObject({
        {"daysAgo", std::max<int64_t>(0, (now - record.startedAt) / DAY)},
        {"attempts", record.attempts},
        {"bestPercent", record.bestPercent},
        {"practice", record.practice},
        {"startPos", record.startPos},
        {"topDeaths", deaths},
        {"topRuns", runs},
    }));
  }

  return matjson::makeObject({{"sessions", sessions}});
}

std::string ProgressStore::describeAgo(int64_t startedAt)
{
  auto days = (std::time(nullptr) - startedAt) / DAY;
  if (days <= 0)
    return "today";
  if (days == 1)
    return "yesterday";
  return fmt::format("{} days ago", days);
}

// Turning saving off deletes what is already on disk
$execute
{
  listenForSettingChanges<bool>("save-progress", [](bool enabled)
                                {
    if (enabled)
      return;

    data() = matjson::makeObject({});
    std::error_code error;
    std::filesystem::remove(filePath(), error); });
}
