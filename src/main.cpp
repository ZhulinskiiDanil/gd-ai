#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "live/LiveTalk.hpp"
#include "popups/ChatPopup/index.hpp"
#include "popups/SessionPopup/index.hpp"
#include "popups/SetupPopup/index.hpp"
#include "store/SessionStore.hpp"

using namespace geode::prelude;

// Waits for the scene transition, a popup shown during it lands in the old scene
static constexpr float SUMMARY_DELAY = .6f;
// Lets the main menu appear before the setup quiz
static constexpr float SETUP_DELAY = .5f;

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
  if (mod->getSavedValue<bool>(SESSION_SUMMARY_ASKED_KEY))
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
        Mod::get()->setSavedValue(SESSION_SUMMARY_ASKED_KEY, true);

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

  listenForKeybindSettingPresses("live-talk-keybind", [](Keybind const &, bool down, bool repeat, double)
                                 {
    if (down && !repeat)
      live::toggle();
    return false; });
}

class $modify(AskDashMenuLayer, MenuLayer)
{
  bool init()
  {
    if (!MenuLayer::init())
      return false;

    addAskDashButton(this, "bottom-menu", CircleBaseSize::MediumAlt);
    showSetupLater();
    return true;
  }

  // The first time the main menu opens with the mod
  void showSetupLater()
  {
    if (!SetupPopup::shouldShow())
      return;

    runAction(CCSequence::create(
        CCDelayTime::create(SETUP_DELAY),
        CallFuncExt::create([]
                            {
          auto scene = CCDirector::get()->getRunningScene();
          if (!SetupPopup::shouldShow() || !scene || scene->getChildByType<SetupPopup>(0))
            return;

          if (auto popup = SetupPopup::create())
            popup->show(); }),
        nullptr));
  }
};

static CCSprite *createLiveTalkSprite(bool active)
{
  auto spr = CircleButtonSprite::createWithSprite(
      "logo.png"_spr, 1.f, active ? CircleBaseColor::Green : CircleBaseColor::Gray, CircleBaseSize::Small);

  auto label = CCLabelBMFont::create("LIVE", "bigFont.fnt");
  label->setScale(.3f);
  spr->addChildAtPosition(label, Anchor::Bottom, {0.f, 4.f});
  return spr;
}

class $modify(AskDashPauseLayer, PauseLayer)
{
  void customSetup()
  {
    PauseLayer::customSetup();
    addAskDashButton(this, "right-button-menu", CircleBaseSize::Small);
    addLiveTalkButton();
  }

  // Green while live talk is on. It also starts after the consent popup and stops by itself, so it is polled
  void addLiveTalkButton()
  {
    auto menu = getChildByID("right-button-menu");
    if (!menu)
      return;

    auto shown = std::make_shared<bool>(live::isActive());
    auto btn = CCMenuItemExt::createSpriteExtra(createLiveTalkSprite(*shown), [](auto)
                                                { live::toggle(); });
    btn->setID("live-talk-button"_spr);

    btn->runAction(CCRepeatForever::create(CCSequence::create(
        CCDelayTime::create(.1f),
        CallFuncExt::create([btn, shown]
                            {
          if (live::isActive() == *shown)
            return;
          *shown = !*shown;
          btn->setSprite(createLiveTalkSprite(*shown)); }),
        nullptr)));

    menu->addChild(btn);
    menu->updateLayout();
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
