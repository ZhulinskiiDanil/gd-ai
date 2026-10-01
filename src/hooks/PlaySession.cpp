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
    // Live talk: how long the attempt runs (paused time doesn't count) and the moments already told
    float runSeconds = 0.f;
    int milestonesSent = 0;
    bool pauseSent = false;
  };

  // Live talk doesn't comment tiny bests like 3%
  static constexpr int MIN_BEST_TO_TELL = 10;
  // Mid-run moments: close to the best, and the spot of the most deaths once it has this many
  static constexpr int NEAR_BEST_PERCENT = 5;
  static constexpr int MIN_HOTSPOT_DEATHS = 3;

  enum Milestone
  {
    PastBest = 1 << 0,
    NearBest = 1 << 1,
    Halfway = 1 << 2,
    Hotspot = 1 << 3,
  };

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
    m_fields->runSeconds = 0.f;
    m_fields->milestonesSent = 0;
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
    bool newBest = isFromZero() && percent > m_fields->levelBest;
    if (newBest)
      m_fields->levelBest = percent;

    if (!live::isActive())
      return;

    if (newBest && percent >= MIN_BEST_TO_TELL)
    {
      live::sendEvent(matjson::makeObject({{"kind", "best"}, {"percent", percent}, {"attempt", m_attempts}}));
      return;
    }

    live::sendEvent(matjson::makeObject({
        {"kind", "death"},
        {"percent", percent},
        {"attempt", m_attempts},
        {"from", m_fields->attemptStart},
        {"practice", static_cast<bool>(m_isPracticeMode)},
        {"runSeconds", static_cast<int>(m_fields->runSeconds)},
    }));
  }

  // ! --- Live talk: moments during a run, each told once per attempt --- !

  void postUpdate(float dt)
  {
    PlayLayer::postUpdate(dt);
    if (!live::isActive())
      return;

    auto fields = m_fields.self();

    // Updates only run unpaused, whatever way the game was resumed
    if (fields->pauseSent)
    {
      fields->pauseSent = false;
      live::sendEvent(matjson::makeObject({{"kind", "resume"}}));
    }

    // Percent means nothing in platformer levels
    if (m_isPlatformer || m_player1->m_isDead)
      return;

    fields->runSeconds += dt;
    int percent = getCurrentPercentInt();
    int best = fields->levelBest;
    int start = fields->attemptStart;
    bool fromZero = isFromZero();

    if (fromZero && best >= MIN_BEST_TO_TELL && percent > best)
      sendMilestone(PastBest, "past-best", percent);
    else if (fromZero && best >= 20 && percent >= best - NEAR_BEST_PERCENT)
      sendMilestone(NearBest, "near-best", percent);

    // Past the best is told by itself, halfway is for runs that are already beyond it
    if (start < 50 && percent >= 50 && (best >= 50 || !fromZero))
      sendMilestone(Halfway, "halfway", percent);

    if (auto const &session = SessionStore::current())
    {
      auto top = session->topDeaths(1);
      if (!top.empty() && top[0].second >= MIN_HOTSPOT_DEATHS && top[0].first > start && percent > top[0].first)
        sendMilestone(Hotspot, "hotspot", top[0].first);
    }
  }

  void sendMilestone(Milestone milestone, char const *name, int percent)
  {
    auto fields = m_fields.self();
    if (fields->milestonesSent & milestone)
      return;
    fields->milestonesSent |= milestone;

    live::sendEvent(matjson::makeObject({
        {"kind", "run"},
        {"milestone", name},
        {"percent", percent},
        {"best", fields->levelBest},
        {"from", fields->attemptStart},
    }));
  }

  // AskDash doesn't speak up on its own while the game is paused
  void pauseGame(bool unfocused)
  {
    PlayLayer::pauseGame(unfocused);
    if (!live::isActive() || m_fields->pauseSent)
      return;

    m_fields->pauseSent = true;
    live::sendEvent(matjson::makeObject({{"kind", "pause"}}));
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
