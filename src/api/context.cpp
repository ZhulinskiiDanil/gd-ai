#include "context.hpp"

using namespace geode::prelude;

static std::string getDifficultyName(GJGameLevel *level)
{
  if (level->m_autoLevel)
    return "Auto";

  if (level->m_demon.value() > 0)
  {
    switch (level->m_demonDifficulty)
    {
    case 3: return "Easy Demon";
    case 4: return "Medium Demon";
    case 5: return "Insane Demon";
    case 6: return "Extreme Demon";
    default: return "Hard Demon";
    }
  }

  switch (level->getAverageDifficulty())
  {
  case 1: return "Easy";
  case 2: return "Normal";
  case 3: return "Hard";
  case 4: return "Harder";
  case 5: return "Insane";
  default: return "Unrated";
  }
}

static std::string getLengthName(int length)
{
  switch (length)
  {
  case 0: return "Tiny";
  case 1: return "Short";
  case 2: return "Medium";
  case 3: return "Long";
  case 4: return "XL";
  case 5: return "Platformer";
  default: return "Unknown";
  }
}

matjson::Value api::context::collect()
{
  std::string scene = "menu";
  GJGameLevel *level = nullptr;

  if (auto play = PlayLayer::get())
  {
    scene = "playing";
    level = play->m_level;
  }
  else if (auto editor = LevelEditorLayer::get())
  {
    scene = "editor";
    level = editor->m_level;
  }
  else if (auto running = CCDirector::get()->getRunningScene())
  {
    if (auto info = running->getChildByType<LevelInfoLayer>(0))
    {
      scene = "level-info";
      level = info->m_level;
    }
  }

  auto context = matjson::makeObject({
      {"scene", scene},
      {"player", std::string(GameManager::get()->m_playerName)},
  });

  if (level)
  {
    context["level"] = matjson::makeObject({
        {"name", std::string(level->m_levelName)},
        {"id", level->m_levelID.value()},
        {"creator", std::string(level->m_creatorName)},
        {"difficulty", getDifficultyName(level)},
        {"stars", level->m_stars.value()},
        {"length", getLengthName(level->m_levelLength)},
        {"attempts", level->m_attempts.value()},
        {"bestPercent", level->m_normalPercent.value()},
        {"practicePercent", level->m_practicePercent},
    });
  }

  return context;
}
