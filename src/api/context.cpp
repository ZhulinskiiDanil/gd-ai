#include "context.hpp"
#include "../integrations/deathTracker.hpp"
#include "../store/ProgressStore.hpp"
#include "../store/SessionStore.hpp"
#include "../store/levelKey.hpp"

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

static void addSong(matjson::Value &json, GJGameLevel *level)
{
  // Custom songs have an ID, official ones only an audio track
  if (level->m_songID <= 0)
  {
    json["songName"] = std::string(LevelTools::getAudioTitle(level->m_audioTrack));
    return;
  }

  json["songId"] = level->m_songID;

  if (auto song = MusicDownloadManager::sharedState()->getSongInfoObject(level->m_songID))
  {
    json["songName"] = std::string(song->m_songName);
    json["songArtist"] = std::string(song->m_artistName);
  }
}

static matjson::Value collectStats()
{
  auto stats = GameStatsManager::sharedState();
  auto stat = [stats](StatKey key)
  { return stats->getStat(std::to_string(static_cast<int>(key)).c_str()); };

  return matjson::makeObject({
      {"stars", stat(StatKey::Stars)},
      {"moons", stat(StatKey::Moons)},
      {"diamonds", stat(StatKey::Diamonds)},
      {"secretCoins", stat(StatKey::Coins)},
      {"userCoins", stat(StatKey::UserCoins)},
      {"demons", stat(StatKey::Demons)},
      {"completedOnline", stat(StatKey::CustomLevels)},
      {"attempts", stat(StatKey::Attempts)},
      {"jumps", stat(StatKey::Jumps)},
  });
}

static matjson::Value collectProfiles()
{
  auto profiles = matjson::makeObject({});

  for (auto list : {"demonlist", "pointercrate"})
  {
    auto username = utils::string::trim(
        Mod::get()->getSettingValue<std::string>(fmt::format("{}-username", list)));
    if (!username.empty())
      profiles[list] = username;
  }

  return profiles;
}

// The level being played, or the last time it was played this game session
static std::optional<LevelSession> findSession(GJGameLevel *level)
{
  if (auto play = PlayLayer::get(); play && SessionStore::current())
  {
    auto session = *SessionStore::current();
    session.attempts = play->m_attempts;
    return session;
  }

  if (auto const &last = SessionStore::last(); last && last->isOf(level))
    return *last;

  return std::nullopt;
}

api::context::Scene api::context::currentScene()
{
  if (auto play = PlayLayer::get())
    return {"playing", play->m_level};

  if (auto editor = LevelEditorLayer::get())
    return {"editor", editor->m_level};

  if (auto running = CCDirector::get()->getRunningScene())
  {
    if (auto info = running->getChildByType<LevelInfoLayer>(0))
      return {"level-info", info->m_level};

    if (auto edit = running->getChildByType<EditLevelLayer>(0))
      return {"level-edit", edit->m_level};
  }

  return {"menu", nullptr};
}

matjson::Value api::context::collect()
{
  auto [scene, level] = currentScene();

  auto context = matjson::makeObject({
      {"scene", scene},
      {"player", std::string(GameManager::get()->m_playerName)},
      {"stats", collectStats()},
      {"profiles", collectProfiles()},
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
    addSong(context["level"], level);

    if (auto deaths = integrations::deathTracker::collect(level))
      context["deathTracker"] = *deaths;

    auto session = findSession(level);
    if (session)
      context["session"] = SessionStore::toJson(*session);

    // Past sessions, without the one already sent as "session"
    if (auto progress = ProgressStore::toJson(levelKey(level), session ? session->startedAt : 0))
      context["progress"] = *progress;
  }

  return context;
}
