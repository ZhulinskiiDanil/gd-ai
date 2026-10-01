#pragma once

#include <Geode/Geode.hpp>

#include "../../store/SessionStore.hpp"

using namespace geode::prelude;

// Shown after leaving a level: attempts, best percent and where you died the most
class SessionPopup : public geode::Popup
{
private:
  LevelSession m_session;
  CCMenuItemToggler *m_dontShowToggle = nullptr;

  bool init(LevelSession session);

  void addStatCards(float centerY);
  void addDeathsChart(float bottomY);
  void addRunsLine(float centerY);
  void addFooter();

  void onAsk(CCObject *);
  void onClose(CCObject *) override;

public:
  static SessionPopup *create(LevelSession session);
};
