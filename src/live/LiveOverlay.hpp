#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Label.hpp>

#include "LiveTalk.hpp"

using namespace geode::prelude;

// Live talk status in the top left corner and what AskDash says at the bottom, on the PlayLayer UI
class LiveOverlay : public CCNode
{
private:
  CCNode *m_pill = nullptr;
  CCScale9Sprite *m_pillBg = nullptr;
  CCDrawNode *m_dot = nullptr;
  CCLabelBMFont *m_status = nullptr;

  CCNode *m_subtitle = nullptr;
  CCScale9Sprite *m_subtitleBg = nullptr;
  geode::Label *m_subtitleText = nullptr;

  live::State m_shownState = live::State::Off;
  std::string m_shownSubtitle;
  // Seconds since AskDash stopped talking, the subtitle fades after a while
  float m_quietTime = 0.f;
  float m_time = 0.f;

  bool init() override;
  void update(float dt) override;

  void showState(live::State state);
  void showSubtitle(std::string const &text);

public:
  static LiveOverlay *create();
};
