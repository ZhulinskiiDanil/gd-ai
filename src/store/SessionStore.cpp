#include "SessionStore.hpp"

#include <algorithm>

static constexpr size_t JSON_TOP_DEATHS = 5;

template <class Key>
static std::vector<std::pair<Key, int>> mostCommon(std::map<Key, int> const &counts, size_t count)
{
  std::vector<std::pair<Key, int>> top(counts.begin(), counts.end());
  std::stable_sort(top.begin(), top.end(), [](auto const &a, auto const &b)
                   { return a.second > b.second; });

  if (top.size() > count)
    top.resize(count);
  return top;
}

std::vector<std::pair<int, int>> LevelSession::topDeaths(size_t count) const
{
  return mostCommon(deaths, count);
}

std::vector<std::pair<std::pair<int, int>, int>> LevelSession::topRuns(size_t count) const
{
  return mostCommon(runs, count);
}

bool LevelSession::isOf(GJGameLevel *level) const
{
  return level && level->m_levelID.value() == levelId &&
         std::string(level->m_levelName) == levelName;
}

std::optional<LevelSession> &SessionStore::current()
{
  static std::optional<LevelSession> session;
  return session;
}

std::optional<LevelSession> &SessionStore::last()
{
  static std::optional<LevelSession> session;
  return session;
}

bool &SessionStore::summaryPending()
{
  static bool pending = false;
  return pending;
}

matjson::Value SessionStore::toJson(LevelSession const &session)
{
  auto top = matjson::Value::array();
  for (auto [percent, count] : session.topDeaths(JSON_TOP_DEATHS))
    top.push(matjson::makeObject({{"percent", percent}, {"deaths", count}}));

  auto runs = matjson::Value::array();
  for (auto [run, count] : session.topRuns(JSON_TOP_DEATHS))
    runs.push(matjson::makeObject({{"from", run.first}, {"to", run.second}, {"count", count}}));

  return matjson::makeObject({
      {"levelId", session.levelId},
      {"attempts", session.attempts},
      {"bestPercent", session.bestPercent},
      {"practice", session.practice},
      {"startPos", session.startPos},
      {"topDeaths", top},
      {"topRuns", runs},
  });
}
