#include "index.hpp"

static constexpr float POPUP_WIDTH = 320.f;
static constexpr float POPUP_HEIGHT = 230.f;
static constexpr float PADDING = 12.f;
static constexpr size_t MAX_KEY_LENGTH = 64;

PlansPopup *PlansPopup::create(std::function<void(billing::Status const &)> onStatus)
{
  auto ret = new PlansPopup();

  if (ret->init(std::move(onStatus)))
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool PlansPopup::init(std::function<void(billing::Status const &)> onStatus)
{
  if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

  m_onStatus = std::move(onStatus);
  setTitle("AskDash Plans");

  // ! --- Current plan --- !
  m_statusLabel = CCLabelBMFont::create("Loading...", "goldFont.fnt");
  m_statusLabel->setScale(.55f);
  m_statusLabel->setPosition({m_size.width / 2, m_size.height - 50.f});
  m_mainLayer->addChild(m_statusLabel);

  // ! --- Paid plans --- !
  m_plansNode = CCNode::create();
  m_plansNode->setPosition({m_size.width / 2, m_size.height - 78.f});
  m_mainLayer->addChild(m_plansNode);

  auto menu = CCMenu::create();
  menu->setPosition({0.f, 0.f});
  m_mainLayer->addChild(menu);

  auto buySpr = ButtonSprite::create("Buy a plan", "goldFont.fnt", "GJ_button_01.png", .8f);
  buySpr->setScale(.7f);
  m_buyBtn = CCMenuItemSpriteExtra::create(buySpr, this, menu_selector(PlansPopup::onBuy));
  m_buyBtn->setPosition({m_size.width / 2, 88.f});
  m_buyBtn->setVisible(false);
  menu->addChild(m_buyBtn);

  // ! --- Key activation --- !
  auto activateSpr = ButtonSprite::create("Activate", "goldFont.fnt", "GJ_button_02.png", .8f);
  activateSpr->setScale(.6f);
  m_activateBtn = CCMenuItemSpriteExtra::create(
      activateSpr, this, menu_selector(PlansPopup::onActivate));

  float activateWidth = activateSpr->getScaledContentWidth();
  m_keyInput = TextInput::create(
      m_size.width - PADDING * 3 - activateWidth, "Key or code", "chatFont.fnt");
  m_keyInput->setMaxCharCount(MAX_KEY_LENGTH);
  m_keyInput->setCommonFilter(CommonFilter::Any);
  m_keyInput->setAnchorPoint({0.f, 0.f});
  m_keyInput->setPosition({PADDING, PADDING + 18.f});
  m_mainLayer->addChild(m_keyInput);

  m_activateBtn->setPosition({
      m_size.width - PADDING - activateWidth / 2,
      m_keyInput->getPositionY() + m_keyInput->getContentHeight() / 2,
  });
  menu->addChild(m_activateBtn);

  auto hint = CCLabelBMFont::create("Bought a plan? Paste your key here", "chatFont.fnt");
  hint->setScale(.5f);
  hint->setOpacity(150);
  hint->setPosition({m_size.width / 2, PADDING + 6.f});
  m_mainLayer->addChild(hint);

  loadStatus();
  loadPlans();

  return true;
}

void PlansPopup::showStatus(billing::Status const &status)
{
  m_statusLabel->setString(billing::describe(status).c_str());
  m_onStatus(status);
}

void PlansPopup::showPlans(billing::Plans const &plans)
{
  m_plansNode->removeAllChildren();

  float y = 0.f;
  for (auto const &plan : plans.plans)
  {
    auto label = CCLabelBMFont::create(
        fmt::format("{}: {}", plan.name, plan.price).c_str(), "bigFont.fnt");
    label->setScale(.4f);
    label->setPositionY(y);
    m_plansNode->addChild(label);
    y -= 18.f;
  }

  auto note = CCLabelBMFont::create(
      "Prices just cover the AI costs. Thanks for the support!", "chatFont.fnt");
  note->setScale(.5f);
  note->setOpacity(150);
  note->setPositionY(y);
  m_plansNode->addChild(note);

  m_buyUrl = plans.buyUrl;
  m_buyBtn->setVisible(!m_buyUrl.empty());
}

void PlansPopup::loadStatus()
{
  m_statusTask.spawn(billing::fetchStatus(), [this](Result<web::WebResponse> response)
                     {
    auto status = billing::parseStatus(response);
    if (status.isErr())
    {
      m_statusLabel->setString(status.unwrapErr().message.c_str());
      m_statusLabel->limitLabelWidth(m_size.width - PADDING * 2, .55f, .2f);
      return;
    }
    showStatus(status.unwrap()); });
}

void PlansPopup::loadPlans()
{
  m_plansTask.spawn(billing::fetchPlans(), [this](Result<web::WebResponse> response)
                    {
    auto plans = billing::parsePlans(response);
    if (plans.isOk())
    {
      showPlans(plans.unwrap());
      return;
    }

    auto error = CCLabelBMFont::create(plans.unwrapErr().message.c_str(), "chatFont.fnt");
    error->setColor({255, 110, 110});
    error->limitLabelWidth(m_size.width - PADDING * 2, .5f, .2f);
    m_plansNode->addChild(error); });
}

void PlansPopup::onBuy(CCObject *)
{
  if (!m_buyUrl.empty())
    web::openLinkInBrowser(m_buyUrl);
}

void PlansPopup::onActivate(CCObject *)
{
  auto key = utils::string::trim(m_keyInput->getString());
  if (key.empty())
    return;

  m_activateBtn->setEnabled(false);
  m_activateTask.spawn(billing::activateKey(key), [this](Result<web::WebResponse> response)
                       {
    m_activateBtn->setEnabled(true);

    auto status = billing::parseStatus(response);
    if (status.isErr())
    {
      Notification::create(status.unwrapErr().message, NotificationIcon::Error)->show();
      return;
    }

    m_keyInput->setString("");
    showStatus(status.unwrap());
    Notification::create("Plan activated. Thanks!", NotificationIcon::Success)->show(); });
}
