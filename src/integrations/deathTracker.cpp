#include "deathTracker.hpp"

using namespace geode::prelude;

static constexpr auto MOD_ID = "elohmrow.death_tracker";
static constexpr auto STATS_FILE = "general.dt";
// Runs are "start-end" pairs, only the most common ones are worth sending
static constexpr size_t MAX_RUNS = 15;
static constexpr size_t MAX_NEW_BESTS = 20;

// Same folder name Death Tracker uses (StatsManager::getLevelKey)
static std::optional<std::string> getLevelKey(GJGameLevel *level)
{
  // Editor levels are keyed by Level ID API ids, which this mod doesn't have
  if (level->m_levelType == GJLevelType::Editor)
    return std::nullopt;

  auto key = std::to_string(level->m_levelID.value());

  if (level->m_levelType == GJLevelType::Main)
    key += "-local";
  if (level->m_dailyID > 0)
    key += "-daily";
  if (level->m_gauntletLevel)
    key += "-gauntlet";

  return key;
}

// { "47": 12, ... } -> [{ percent: "47", count: 12 }, ...] sorted by the leading number
static matjson::Value sortedPairs(matjson::Value const &map, char const *keyName)
{
  std::vector<std::pair<std::string, int>> items;
  for (auto const &item : map)
    items.emplace_back(item.getKey().value_or(""), item.asInt().unwrapOr(0));

  auto leading = [](std::string const &key)
  { return utils::numFromString<int>(key.substr(0, key.find('-', 1))).unwrapOr(0); };
  std::ranges::sort(items, {}, [&](auto const &item)
                    { return std::pair(leading(item.first), item.first); });

  auto json = matjson::Value::array();
  for (auto const &[key, count] : items)
    json.push(matjson::makeObject({{keyName, key}, {"count", count}}));
  return json;
}

std::optional<matjson::Value> integrations::deathTracker::collect(GJGameLevel *level)
{
  if (!level || !Mod::get()->getSettingValue<bool>("use-death-tracker"))
    return std::nullopt;

  auto tracker = Loader::get()->getLoadedMod(MOD_ID);
  auto key = getLevelKey(level);
  if (!tracker || !key)
    return std::nullopt;

  auto folder = tracker->getSettingValue<std::filesystem::path>("save-path-new");
  auto stats = utils::file::readJson(folder / *key / STATS_FILE);
  if (stats.isErr())
    return std::nullopt;

  auto const &json = stats.unwrap();

  int total = 0;
  for (auto const &count : json["deaths"])
    total += count.asInt().unwrapOr(0);

  // Most common practice / start pos runs first
  auto runs = sortedPairs(json["runs"], "run");
  if (runs.isArray())
  {
    auto &list = runs.asArray().unwrap();
    std::ranges::sort(list, std::greater{}, [](auto const &run)
                      { return run["count"].asInt().unwrapOr(0); });
    if (list.size() > MAX_RUNS)
      list.resize(MAX_RUNS);
  }

  auto newBests = matjson::Value::array();
  for (auto const &best : json["newBests"])
    newBests.push(best.asInt().unwrapOr(0));
  if (newBests.size() > MAX_NEW_BESTS)
  {
    auto &list = newBests.asArray().unwrap();
    list.erase(list.begin(), list.end() - MAX_NEW_BESTS);
  }

  return matjson::makeObject({
      {"totalDeaths", total},
      {"deaths", sortedPairs(json["deaths"], "percent")},
      {"topRuns", runs},
      {"newBests", newBests},
  });
}
