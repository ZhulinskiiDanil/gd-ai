#pragma once

#include <string>
#include <Geode/Geode.hpp>

// The same level across game sessions: its ID, or its name for local levels (they all have ID 0)
inline std::string levelKey(int levelId, std::string const &levelName)
{
  return levelId > 0 ? std::to_string(levelId) : "local:" + levelName;
}

inline std::string levelKey(GJGameLevel *level)
{
  return levelKey(level->m_levelID.value(), std::string(level->m_levelName));
}
