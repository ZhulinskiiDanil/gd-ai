#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "../live/LiveOverlay.hpp"
#include "../live/LiveTalk.hpp"
#include "../store/ProgressStore.hpp"
#include "../store/SessionStore.hpp"

#include <ctime>

using namespace geode::prelude;

// Counts attempts and deaths for the session summary, tells live talk what happens
class $modify(AskDashPlayLayer, PlayLayer)
{
  struct Fields
  {
    // Where the current attempt started: a start pos, a practice checkpoint or 0
    int attemptStart = 0;
    // Best percent from 0 before this attempt, GD updates the level's own while the player dies
    int levelBest = 0;
  };

  // Live talk doesn't comment tiny bests like 3%
  static constexpr int MIN_BEST_TO_TELL = 10;

  bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
  {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
      return false;

    SessionStore::current() = LevelSession{
        .levelId = level->m_levelID.value(),
        .levelName = std::string(level->m_levelName),
        .startedAt = std::time(nullptr),
    };
    m_fields->levelBest = level->m_normalPercent.value();

    m_uiLayer->addChild(LiveOverlay::create(), 100);
    return true;
  }

  void resetLevel()
  {
    PlayLayer::resetLevel();
    m_fields->attemptStart = getCurrentPercentInt();
  }

  // Only attempts from 0% outside practice count for deaths and the best percent
  bool isFromZero()
  {
    return m_fields->attemptStart == 0 && !m_isPracticeMode;
  }

  void destroyPlayer(PlayerObject *player, GameObject *object)
  {
    bool wasDead = m_player1->m_isDead;
    PlayLayer::destroyPlayer(player, object);

    auto &session = SessionStore::current();
    if (!session || object == m_anticheatSpike || wasDead || !m_player1->m_isDead)
      return;

    int percent = getCurrentPercentInt();
    session->practice = session->practice || m_isPracticeMode;
    session->startPos = session->startPos || m_isTestMode;

    if (isFromZero())
    {
      session->deaths[percent]++;
      session->bestPercent = std::max(session->bestPercent, percent);
    }
    else
      session->runs[{m_fields->attemptStart, percent}]++;

    // ! --- Live talk --- !
    if (!live::isActive())
      return;

    if (isFromZero() && percent > m_fields->levelBest && percent >= MIN_BEST_TO_TELL)
    {
      m_fields->levelBest = percent;
      live::sendEvent(matjson::makeObject({{"kind", "best"}, {"percent", percent}, {"attempt", m_attempts}}));
      return;
    }

    live::sendEvent(matjson::makeObject({
        {"kind", "death"},
        {"percent", percent},
        {"attempt", m_attempts},
        {"from", m_fields->attemptStart},
        {"practice", static_cast<bool>(m_isPracticeMode)},
    }));
  }

  void levelComplete()
  {
    PlayLayer::levelComplete();

    live::sendEvent(matjson::makeObject({
        {"kind", "complete"},
        {"attempts", m_attempts},
        {"practice", static_cast<bool>(m_isPracticeMode)},
    }));

    auto &session = SessionStore::current();
    if (!session)
      return;

    if (isFromZero())
      session->bestPercent = 100;
    else
      session->runs[{m_fields->attemptStart, 100}]++;
  }

  void onQuit()
  {
    live::stop();

    if (auto &session = SessionStore::current())
    {
      session->attempts = m_attempts;
      ProgressStore::record(*session);

      auto mod = Mod::get();
      SessionStore::summaryPending() =
          mod->getSettingValue<bool>("session-summary") &&
          session->attempts >= mod->getSettingValue<int64_t>("session-min-attempts");

      SessionStore::last() = std::move(session);
      session.reset();
    }

    PlayLayer::onQuit();
  }
};
