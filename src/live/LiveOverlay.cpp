#include "LiveOverlay.hpp"

#include <algorithm>

static constexpr float MARGIN = 8.f;
static constexpr float PILL_HEIGHT = 18.f;
static constexpr float DOT_RADIUS = 3.5f;
static constexpr float SUBTITLE_Y = 46.f;
static constexpr float SUBTITLE_SCALE = .7f;
static constexpr size_t SUBTITLE_MAX_CHARS = 160;
static constexpr float SUBTITLE_HOLD = 3.f;
static constexpr float SUBTITLE_FADE = .5f;

LiveOverlay *LiveOverlay::create()
{
  auto ret = new LiveOverlay();
  if (ret->init())
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool LiveOverlay::init()
{
  if (!CCNode::init())
    return false;

  auto winSize = CCDirector::get()->getWinSize();
  setContentSize(winSize);
  setID("live-overlay"_spr);

  // ! --- Status pill --- !
  m_pill = CCNode::create();
  m_pill->setAnchorPoint({0.f, 1.f});
  m_pill->setPosition({MARGIN, winSize.height - MARGIN});
  addChild(m_pill);

  m_pillBg = CCScale9Sprite::create("square02_small.png");
  m_pillBg->setOpacity(150);
  m_pillBg->setAnchorPoint({0.f, 0.f});
  m_pill->addChild(m_pillBg);

  m_dot = CCDrawNode::create();
  m_dot->setPosition({PILL_HEIGHT / 2, PILL_HEIGHT / 2});
  m_pill->addChild(m_dot);

  m_status = CCLabelBMFont::create("", "chatFont.fnt");
  m_status->setScale(.55f);
  m_status->setAnchorPoint({0.f, .5f});
  m_status->setPosition({PILL_HEIGHT, PILL_HEIGHT / 2});
  m_pill->addChild(m_status);

  // ! --- Subtitle --- !
  m_subtitle = CCNode::create();
  m_subtitle->setPosition({winSize.width / 2, SUBTITLE_Y});
  m_subtitle->setVisible(false);
  addChild(m_subtitle);

  m_subtitleBg = CCScale9Sprite::create("square02_small.png");
  m_subtitleBg->setOpacity(140);
  m_subtitle->addChild(m_subtitleBg);

  m_subtitleText = geode::Label::create("", "chatFont.fnt");
  m_subtitleText->setScale(SUBTITLE_SCALE);
  m_subtitleText->setMaxWidth(winSize.width * .6f / SUBTITLE_SCALE);
  m_subtitleText->setAlignment(geode::Label::Alignment::Center);
  m_subtitle->addChild(m_subtitleText);

  setVisible(false);
  scheduleUpdate();
  return true;
}

void LiveOverlay::update(float dt)
{
  m_time += dt;
  auto state = live::state();
  setVisible(state != live::State::Off);

  if (state != m_shownState)
    showState(state);

  // Connecting blinks, speaking pulses
  if (state == live::State::Connecting)
    m_dot->setScale(std::fmod(m_time, 1.f) < .5f ? 1.f : .4f);
  else if (state == live::State::Speaking)
    m_dot->setScale(1.f + .25f * std::sin(m_time * 10.f));
  else
    m_dot->setScale(1.f);

  if (live::subtitle() != m_shownSubtitle)
    showSubtitle(live::subtitle());

  // The subtitle stays a moment after AskDash is quiet, then fades
  m_quietTime = state == live::State::Speaking ? 0.f : m_quietTime + dt;
  if (m_subtitle->isVisible())
  {
    float fade = std::clamp(1.f - (m_quietTime - SUBTITLE_HOLD) / SUBTITLE_FADE, 0.f, 1.f);
    m_subtitleBg->setOpacity(static_cast<GLubyte>(140 * fade));
    m_subtitleText->setOpacity(static_cast<GLubyte>(255 * fade));
  }
}

void LiveOverlay::showState(live::State state)
{
  m_shownState = state;

  char const *text = "";
  ccColor4F color = {1.f, 1.f, 1.f, 1.f};
  switch (state)
  {
  case live::State::Off:
    break;
  case live::State::Connecting:
    text = "Connecting...";
    color = {1.f, .85f, .2f, 1.f};
    break;
  case live::State::Listening:
    text = "AskDash is listening";
    color = {.35f, .9f, .4f, 1.f};
    break;
  case live::State::Thinking:
    text = "AskDash is thinking...";
    color = {.3f, .8f, 1.f, 1.f};
    break;
  case live::State::Speaking:
    text = "AskDash is talking";
    color = {1.f, .45f, .9f, 1.f};
    break;
  }

  m_status->setString(text);
  m_dot->clear();
  m_dot->drawDot({0.f, 0.f}, DOT_RADIUS, color);

  float width = PILL_HEIGHT + m_status->getScaledContentWidth() + 8.f;
  m_pillBg->setContentSize({width, PILL_HEIGHT});
  m_pill->setContentSize({width, PILL_HEIGHT});
}

// Typographic quotes, dashes and ellipses as plain ASCII
static std::string toAscii(std::string text)
{
  static constexpr std::pair<std::string_view, std::string_view> REPLACEMENTS[] = {
      {"\xE2\x80\x98", "'"},
      {"\xE2\x80\x99", "'"},
      {"\xE2\x80\x9C", "\""},
      {"\xE2\x80\x9D", "\""},
      {"\xE2\x80\x93", "-"},
      {"\xE2\x80\x94", "-"},
      {"\xE2\x80\xA6", "..."},
  };

  for (auto [from, to] : REPLACEMENTS)
  {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
      text.replace(at, from.size(), to);
  }
  return text;
}

void LiveOverlay::showSubtitle(std::string const &original)
{
  m_shownSubtitle = original;
  auto text = toAscii(original);

  // GD fonts only have basic Latin: replies in other languages are only heard
  bool printable = std::all_of(text.begin(), text.end(), [](unsigned char c)
                               { return c >= 32 && c < 127; });
  if (text.empty() || !printable || !Mod::get()->getSettingValue<bool>("live-talk-subtitles"))
  {
    m_subtitle->setVisible(false);
    return;
  }

  // Long replies show their latest part
  auto shown = text.size() > SUBTITLE_MAX_CHARS ? "..." + text.substr(text.size() - SUBTITLE_MAX_CHARS) : text;
  m_subtitleText->setString(shown.c_str());

  auto size = m_subtitleText->getScaledContentSize();
  m_subtitleBg->setContentSize({size.width + 14.f, size.height + 8.f});
  m_subtitleText->setAnchorPoint({.5f, .5f});
  m_subtitleText->setPosition({0.f, 0.f});
  m_subtitle->setVisible(true);
  m_quietTime = 0.f;
}
