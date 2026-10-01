#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "popups/ChatPopup/index.hpp"
#include "popups/SessionPopup/index.hpp"
#include "store/SessionStore.hpp"

using namespace geode::prelude;

// Waits for the scene transition, a popup shown during it lands in the old scene
static constexpr float SUMMARY_DELAY = .6f;
// Set once the player answered if they want session summaries
static constexpr auto SUMMARY_ASKED_KEY = "session-summary-asked";

static void openChat()
{
  auto scene = CCDirector::get()->getRunningScene();
  if (!scene || scene->getChildByType<ChatPopup>(0))
    return;

  if (auto play = PlayLayer::get(); play && !play->m_isPaused)
    play->pauseGame(false);

  if (auto popup = ChatPopup::create())
    popup->show();
}

static void addAskDashButton(CCNode *parent, char const *menuID, CircleBaseSize size)
{
  auto menu = parent->getChildByID(menuID);
  if (!menu)
    return;

  auto spr = CircleButtonSprite::createWithSprite(
      "logo.png"_spr, 1.f, CircleBaseColor::DarkPurple, size);

  auto btn = CCMenuItemExt::createSpriteExtra(spr, [](auto)
                                              { openChat(); });
  btn->setID("askdash-button"_spr);

  menu->addChild(btn);
  menu->updateLayout();
}

// The first summary asks if the player wants them at all, "No" turns the setting off
static void askOnceThenShowSummary(LevelSession const &session)
{
  auto mod = Mod::get();
  if (mod->getSavedValue<bool>(SUMMARY_ASKED_KEY))
  {
    if (auto popup = SessionPopup::create(session))
      popup->show();
    return;
  }

  createQuickPopup(
      "Session summary",
      "After leaving a level, AskDash can show your <cy>attempts</c>, <cy>best percent</c> and "
      "<cy>where you died</c>.\nShow it after each level? You can change this in the mod settings.",
      "No", "Yes",
      [session](auto, bool yes)
      {
        Mod::get()->setSavedValue(SUMMARY_ASKED_KEY, true);

        if (!yes)
        {
          Mod::get()->setSettingValue<bool>("session-summary", false);
          return;
        }

        if (auto popup = SessionPopup::create(session))
          popup->show();
      });
}

// Shows the summary of the session just left on this level, once
static void showSessionSummaryLater(CCNode *layer, GJGameLevel *level)
{
  auto const &last = SessionStore::last();
  if (!SessionStore::summaryPending() || !last || !last->isOf(level))
    return;

  layer->runAction(CCSequence::create(
      CCDelayTime::create(SUMMARY_DELAY),
      CallFuncExt::create([]
                          {
        auto const &last = SessionStore::last();
        if (!SessionStore::summaryPending() || !last)
          return;

        SessionStore::summaryPending() = false;
        askOnceThenShowSummary(*last); }),
      nullptr));
}

$on_game(Loaded)
{
  listenForKeybindSettingPresses("open-keybind", [](Keybind const &, bool down, bool repeat, double)
                                 {
    if (down && !repeat)
      openChat();
    return false; });
}

class $modify(AskDashMenuLayer, MenuLayer)
{
  bool init()
  {
    if (!MenuLayer::init())
      return false;

    addAskDashButton(this, "bottom-menu", CircleBaseSize::MediumAlt);
    return true;
  }
};

class $modify(AskDashPauseLayer, PauseLayer)
{
  void customSetup()
  {
    PauseLayer::customSetup();
    addAskDashButton(this, "right-button-menu", CircleBaseSize::Small);
  }
};

class $modify(AskDashLevelInfoLayer, LevelInfoLayer)
{
  bool init(GJGameLevel *level, bool challenge)
  {
    if (!LevelInfoLayer::init(level, challenge))
      return false;

    addAskDashButton(this, "left-side-menu", CircleBaseSize::Small);
    showSessionSummaryLater(this, level);
    return true;
  }
};

class $modify(AskDashEditLevelLayer, EditLevelLayer)
{
  bool init(GJGameLevel *level)
  {
    if (!EditLevelLayer::init(level))
      return false;

    addAskDashButton(this, "level-actions-menu", CircleBaseSize::Small);
    showSessionSummaryLater(this, level);
    return true;
  }
};

class $modify(AskDashEditorPauseLayer, EditorPauseLayer)
{
  bool init(LevelEditorLayer *layer)
  {
    if (!EditorPauseLayer::init(layer))
      return false;

    addAskDashButton(this, "settings-menu", CircleBaseSize::Small);
    return true;
  }
};
