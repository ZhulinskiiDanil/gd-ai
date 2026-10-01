#include "index.hpp"

#include "../ChatPopup/index.hpp"
#include "../../store/ProgressStore.hpp"
#include "../../store/levelKey.hpp"

#include <numeric>

static constexpr float POPUP_WIDTH = 320.f;
static constexpr float POPUP_HEIGHT = 250.f;
// Without the runs line
static constexpr float POPUP_HEIGHT_SHORT = 232.f;
static constexpr float SIDE = 18.f;

static constexpr float CARD_HEIGHT = 40.f;
static constexpr float CARD_GAP = 8.f;

static constexpr float CHART_HEIGHT = 36.f;
static constexpr float TRACK_HEIGHT = 4.f;
static constexpr size_t HIGHLIGHTED_DEATHS = 3;
static constexpr size_t SHOWN_RUNS = 2;

static constexpr ccColor3B HOT_RED = {255, 85, 85};
static constexpr ccColor3B SOFT_RED = {255, 130, 130};
static constexpr ccColor3B GREEN = {90, 225, 110};
static constexpr ccColor3B GRAY = {200, 200, 200};

static CCLabelBMFont *createCaption(std::string const &text, float scale = .55f)
{
  auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
  label->setScale(scale);
  label->setOpacity(190);
  return label;
}

static CCLayerColor *createRect(ccColor3B color, GLubyte opacity, float width, float height)
{
  auto rect = CCLayerColor::create({color.r, color.g, color.b, opacity}, width, height);
  rect->ignoreAnchorPointForPosition(true);
  return rect;
}

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
  bool hasRuns = !session.runs.empty();
  if (!Popup::init(POPUP_WIDTH, hasRuns ? POPUP_HEIGHT : POPUP_HEIGHT_SHORT))
    return false;

  m_session = std::move(session);
  setTitle(m_session.levelName.empty() ? "Session summary" : m_session.levelName);
  m_title->limitLabelWidth(m_size.width - 80.f, .7f, .3f);

  float top = m_size.height;
  addStatCards(top - 62.f);
  addDeathsChart(top - 150.f);
  if (hasRuns)
    addRunsLine(top - 182.f);
  addFooter();

  return true;
}

// ! --- Stat cards --- !

void SessionPopup::addStatCards(float centerY)
{
  struct Card
  {
    std::string value;
    std::string caption;
    ccColor3B color = {255, 255, 255};
  };

  std::vector<Card> cards = {
      {std::to_string(m_session.attempts), m_session.attempts == 1 ? "Attempt" : "Attempts"},
      {fmt::format("{}%", m_session.bestPercent), "Best from 0"},
  };

  // Compared with the previous session, or the deaths of this one when there is none
  auto history = ProgressStore::history(levelKey(m_session.levelId, m_session.levelName), m_session.startedAt);
  if (!history.empty())
  {
    auto const &previous = history.back();
    int delta = m_session.bestPercent - previous.bestPercent;
    cards.push_back({
        delta > 0 ? fmt::format("+{}%", delta) : delta < 0 ? fmt::format("{}%", delta) : "Same",
        fmt::format("vs {} ({}%)", ProgressStore::describeAgo(previous.startedAt), previous.bestPercent),
        delta > 0 ? GREEN : delta < 0 ? SOFT_RED : GRAY,
    });
  }
  else
  {
    int deaths = std::accumulate(m_session.deaths.begin(), m_session.deaths.end(), 0,
                                 [](int sum, auto const &entry)
                                 { return sum + entry.second; });
    cards.push_back({std::to_string(deaths), "Deaths from 0"});
  }

  float cardWidth = (m_size.width - SIDE * 2 - CARD_GAP * (cards.size() - 1)) / cards.size();

  for (size_t i = 0; i < cards.size(); i++)
  {
    float centerX = SIDE + cardWidth / 2 + i * (cardWidth + CARD_GAP);

    auto bg = CCScale9Sprite::create("square02_small.png");
    bg->setContentSize({cardWidth, CARD_HEIGHT});
    bg->setOpacity(80);
    bg->setPosition({centerX, centerY});
    m_mainLayer->addChild(bg);

    auto value = CCLabelBMFont::create(cards[i].value.c_str(), "bigFont.fnt");
    value->limitLabelWidth(cardWidth - 12.f, .55f, .2f);
    value->setColor(cards[i].color);
    value->setPosition({centerX, centerY + 6.f});
    m_mainLayer->addChild(value);

    auto caption = createCaption(cards[i].caption, .5f);
    caption->limitLabelWidth(cardWidth - 10.f, .5f, .2f);
    caption->setPosition({centerX, centerY - 11.f});
    m_mainLayer->addChild(caption);
  }
}

// ! --- Deaths chart: deaths by percent over a 0-100% track --- !

void SessionPopup::addDeathsChart(float bottomY)
{
  float width = m_size.width - SIDE * 2;
  auto top = m_session.topDeaths(HIGHLIGHTED_DEATHS);

  // Header: what the chart is and where the most deaths are
  auto header = createCaption("Where you died (from 0)");
  header->setAnchorPoint({0.f, .5f});
  header->setPosition({SIDE, bottomY + CHART_HEIGHT + 10.f});
  m_mainLayer->addChild(header);

  if (!top.empty())
  {
    auto most = createCaption(fmt::format("Most at {}% ({})", top[0].first, top[0].second));
    most->setColor(HOT_RED);
    most->setAnchorPoint({1.f, .5f});
    most->setPosition({m_size.width - SIDE, header->getPositionY()});
    m_mainLayer->addChild(most);
  }

  // Bars
  int maxDeaths = top.empty() ? 1 : top[0].second;
  float barWidth = std::max(1.5f, width / 101.f - .6f);

  for (auto [percent, count] : m_session.deaths)
  {
    bool hot = std::any_of(top.begin(), top.end(), [percent](auto const &entry)
                           { return entry.first == percent; });
    float height = std::max(2.f, CHART_HEIGHT * count / maxDeaths);

    auto bar = createRect(hot ? HOT_RED : SOFT_RED, hot ? 255 : 120, barWidth, height);
    bar->setPosition({SIDE + width * percent / 100.f - barWidth / 2, bottomY});
    m_mainLayer->addChild(bar);
  }

  if (m_session.deaths.empty())
  {
    auto empty = createCaption("No deaths from 0 this session");
    empty->setOpacity(140);
    empty->setPosition({m_size.width / 2, bottomY + CHART_HEIGHT / 2});
    m_mainLayer->addChild(empty);
  }

  // Track under the bars, filled up to the best percent
  float trackY = bottomY - TRACK_HEIGHT - 1.f;
  auto track = createRect({0, 0, 0}, 110, width, TRACK_HEIGHT);
  track->setPosition({SIDE, trackY});
  m_mainLayer->addChild(track);

  if (m_session.bestPercent > 0)
  {
    auto fill = createRect(GREEN, 255, width * m_session.bestPercent / 100.f, TRACK_HEIGHT);
    fill->setPosition({SIDE, trackY});
    m_mainLayer->addChild(fill);
  }

  // Axis
  for (int percent : {0, 50, 100})
  {
    auto label = createCaption(fmt::format("{}%", percent), .45f);
    label->setOpacity(130);
    label->setAnchorPoint({percent / 100.f, 1.f});
    label->setPosition({SIDE + width * percent / 100.f, trackY - 2.f});
    m_mainLayer->addChild(label);
  }
}

// ! --- Start pos and practice runs --- !

void SessionPopup::addRunsLine(float centerY)
{
  std::vector<std::string> runs;
  for (auto [run, count] : m_session.topRuns(SHOWN_RUNS))
    runs.push_back(fmt::format("{}-{}% x{}", run.first, run.second, count));

  auto kind = m_session.startPos && m_session.practice ? "Start pos and practice runs"
              : m_session.startPos                    ? "Start pos runs"
                                                      : "Practice runs";

  auto label = createCaption(fmt::format("{}: {}", kind, fmt::join(runs, ", ")));
  label->limitLabelWidth(m_size.width - SIDE * 2, .55f, .3f);
  label->setPosition({m_size.width / 2, centerY});
  m_mainLayer->addChild(label);
}

// ! --- Footer: don't show again and buttons --- !

void SessionPopup::addFooter()
{
  constexpr float y = 24.f;

  auto menu = CCMenu::create();
  menu->setPosition({0.f, 0.f});
  m_mainLayer->addChild(menu);

  m_dontShowToggle = CCMenuItemExt::createTogglerWithStandardSprites(.55f, [](auto) {});
  m_dontShowToggle->setPosition({SIDE + 8.f, y});
  menu->addChild(m_dontShowToggle);

  // The label toggles too, the checkbox alone is a small target
  auto dontShowLabel = createCaption("Don't show again");
  auto dontShowBtn = CCMenuItemExt::createSpriteExtra(dontShowLabel, [this](auto)
                                                      { m_dontShowToggle->toggle(!m_dontShowToggle->isToggled()); });
  dontShowBtn->setPosition({SIDE + 20.f + dontShowBtn->getContentWidth() / 2, y});
  menu->addChild(dontShowBtn);

  auto closeSpr = ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_04.png", .8f);
  closeSpr->setScale(.6f);
  auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(SessionPopup::onClose));
  closeBtn->setPosition({m_size.width - SIDE - closeSpr->getScaledContentWidth() / 2, y});
  menu->addChild(closeBtn);

  auto askSpr = ButtonSprite::create("Ask AskDash", "goldFont.fnt", "GJ_button_01.png", .8f);
  askSpr->setScale(.6f);
  auto askBtn = CCMenuItemSpriteExtra::create(askSpr, this, menu_selector(SessionPopup::onAsk));
  askBtn->setPosition({
      closeBtn->getPositionX() - closeSpr->getScaledContentWidth() / 2 - 6.f - askSpr->getScaledContentWidth() / 2,
      y,
  });
  menu->addChild(askBtn);
}

void SessionPopup::onClose(CCObject *sender)
{
  if (m_dontShowToggle && m_dontShowToggle->isToggled())
  {
    Mod::get()->setSettingValue<bool>("session-summary", false);
    Notification::create("Session summary is off, turn it back on in AskDash settings",
                         NotificationIcon::Info)
        ->show();
  }

  Popup::onClose(sender);
}

void SessionPopup::onAsk(CCObject *sender)
{
  // The summary itself goes in the game context, see api::context::collect
  onClose(sender);

  if (auto popup = ChatPopup::create(
          "I just played this level. Here is my session: where do I struggle, what should I practice and am I improving?"))
    popup->show();
}
