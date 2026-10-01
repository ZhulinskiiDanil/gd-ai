#pragma once

#include <Geode/Geode.hpp>

#include "../../store/SessionStore.hpp"

using namespace geode::prelude;

// Shown after leaving a level: attempts, best percent and where you died the most
class SessionPopup : public geode::Popup
{
private:
  LevelSession m_session;

  bool init(LevelSession session);

  void onAsk(CCObject *);

public:
  static SessionPopup *create(LevelSession session);
};
