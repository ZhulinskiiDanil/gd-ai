#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include "popups/ChatPopup/index.hpp"

using namespace geode::prelude;

static void addAskDashButton(CCNode *parent, char const *menuID, CircleBaseSize size)
{
  auto menu = parent->getChildByID(menuID);
  if (!menu)
    return;

  auto spr = CircleButtonSprite::createWithSprite(
      "logo.png"_spr, 1.f, CircleBaseColor::DarkPurple, size);

  auto btn = CCMenuItemExt::createSpriteExtra(spr, [](auto)
                                              {
    if (auto popup = ChatPopup::create())
      popup->show(); });
  btn->setID("askdash-button"_spr);

  menu->addChild(btn);
  menu->updateLayout();
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
    return true;
  }
};
