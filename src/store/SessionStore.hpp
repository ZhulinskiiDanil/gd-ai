#pragma once

#include <map>
#include <string>
#include <Geode/Geode.hpp>

// Attempts and deaths of one play session of a level, only kept in memory
struct LevelSession
{
  int levelId = 0;
  std::string levelName;
  // Unix seconds, tells this session apart in ProgressStore
  int64_t startedAt = 0;
  int attempts = 0;
  int bestPercent = 0;
  bool practice = false;
  bool startPos = false;
  // Attempts from 0%: percent -> deaths
  std::map<int, int> deaths;
  // Start pos and practice attempts: {start, end} percent -> count
  std::map<std::pair<int, int>, int> runs;

  // Most deaths first
  std::vector<std::pair<int, int>> topDeaths(size_t count) const;
  // Most common first
  std::vector<std::pair<std::pair<int, int>, int>> topRuns(size_t count) const;

  // Local levels all have ID 0, so the name is compared too
  bool isOf(GJGameLevel *level) const;
};

namespace SessionStore
{
  // The level being played right now
  std::optional<LevelSession> &current();

  // The last level left, for the game context
  std::optional<LevelSession> &last();

  // Set when leaving a level, cleared once its summary popup is shown
  bool &summaryPending();

  matjson::Value toJson(LevelSession const &session);
}
