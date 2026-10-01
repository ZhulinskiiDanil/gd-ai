#include "index.hpp"

#include "../ChatPopup/index.hpp"

static constexpr float POPUP_WIDTH = 280.f;
static constexpr float POPUP_HEIGHT = 220.f;
static constexpr size_t SHOWN_DEATHS = 3;
static constexpr size_t SHOWN_RUNS = 2;

SessionPopup *SessionPopup::create(LevelSession session)
{
  auto ret = new SessionPopup();

  if (ret->init(std::move(session)))
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool SessionPopup::init(LevelSession session)
{
  if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

  m_session = std::move(session);
  setTitle(m_session.levelName.empty() ? "Session summary" : m_session.levelName);

  auto lines = CCNode::create();
  lines->setContentSize({m_size.width - 30.f, 135.f});
  lines->setAnchorPoint({.5f, .5f});
  lines->setPosition({m_size.width / 2, m_size.height / 2 + 12.f});
  lines->setLayout(ColumnLayout::create()->setGap(4.f)->setAxisReverse(true));
  m_mainLayer->addChild(lines);

  auto addLine = [&](std::string const &text, float scale)
  {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    lines->addChild(label);
  };

  addLine(fmt::format("{} attempts, best {}% from 0", m_session.attempts, m_session.bestPercent), .45f);

  auto top = m_session.topDeaths(SHOWN_DEATHS);
  auto runs = m_session.topRuns(SHOWN_RUNS);

  if (top.empty() && runs.empty())
    addLine("No deaths this session", .35f);
  else if (!top.empty())
    addLine("Most deaths from 0:", .35f);

  for (auto [percent, count] : top)
    addLine(fmt::format("{}% - {} {}", percent, count, count == 1 ? "death" : "deaths"), .35f);

  if (!runs.empty())
    addLine(m_session.startPos && m_session.practice ? "Start pos and practice runs:"
            : m_session.startPos                    ? "Start pos runs:"
                                                    : "Practice runs:",
            .35f);

  for (auto [run, count] : runs)
    addLine(fmt::format("{}-{}% - {}x", run.first, run.second, count), .35f);

  lines->updateLayout();

  // ! --- Buttons --- !
  auto menu = CCMenu::create();
  menu->setContentSize({m_size.width, 30.f});
  menu->setPosition({m_size.width / 2, 25.f});
  menu->setLayout(RowLayout::create()->setGap(10.f));

  auto askSpr = ButtonSprite::create("Ask AskDash", "goldFont.fnt", "GJ_button_01.png", .8f);
  askSpr->setScale(.7f);
  menu->addChild(CCMenuItemSpriteExtra::create(askSpr, this, menu_selector(SessionPopup::onAsk)));

  auto closeSpr = ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_04.png", .8f);
  closeSpr->setScale(.7f);
  menu->addChild(CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(SessionPopup::onClose)));

  menu->updateLayout();
  m_mainLayer->addChild(menu);

  return true;
}

void SessionPopup::onAsk(CCObject *sender)
{
  // The summary itself goes in the game context, see api::context::collect
  onClose(sender);

  if (auto popup = ChatPopup::create(
          "I just played this level. Here is my session: where do I struggle and what should I practice?"))
    popup->show();
}
