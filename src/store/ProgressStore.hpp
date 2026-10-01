#pragma once

#include <string>
#include <vector>
#include <Geode/Geode.hpp>

#include "SessionStore.hpp"

// A finished play session of a level, kept on disk when "save-progress" is on
struct SessionRecord
{
  // Unix seconds
  int64_t startedAt = 0;
  int attempts = 0;
  int bestPercent = 0;
  bool practice = false;
  bool startPos = false;
  // percent -> deaths
  std::vector<std::pair<int, int>> topDeaths;
  // {start, end} percent -> count
  std::vector<std::pair<std::pair<int, int>, int>> topRuns;
};

namespace ProgressStore
{
  // Saves a session just left
  void record(LevelSession const &session);

  // Past sessions of a level, oldest first, without the one started at `exceptStartedAt`
  std::vector<SessionRecord> history(std::string const &key, int64_t exceptStartedAt = 0);

  // Last sessions for the game context, nullopt when there are none
  std::optional<matjson::Value> toJson(std::string const &key, int64_t exceptStartedAt);

  // "today", "yesterday", "3 days ago"
  std::string describeAgo(int64_t startedAt);
}
