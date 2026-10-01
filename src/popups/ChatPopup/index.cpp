#include "index.hpp"

#include "../../api/chat.hpp"
#include "../../api/context.hpp"
#include "../../actions/navigate.hpp"
#include "../../store/ProgressStore.hpp"
#include "../../store/SessionStore.hpp"
#include "../../store/levelKey.hpp"
#include "../PlansPopup/index.hpp"

#include <regex>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Label.hpp>

static constexpr float POPUP_WIDTH = 400.f;
static constexpr float POPUP_HEIGHT = 270.f;
static constexpr float PADDING = 10.f;
static constexpr float BUBBLE_PADDING = 6.f;
static constexpr float BUBBLE_GAP = 5.f;
static constexpr float TEXT_SCALE = .6f;
static constexpr size_t MAX_INPUT_LENGTH = 500;
static constexpr float ACTION_SCALE = .45f;
static constexpr float ACTION_GAP = 4.f;
static constexpr float COPY_SCALE = .45f;
static constexpr float POLL_INTERVAL = .25f;
static constexpr float PROMPTS_HEIGHT = 16.f;
static constexpr float TITLE_WIDTH = 170.f;
static constexpr size_t MAX_QUICK_PROMPTS = 3;

static bool hasDeaths(std::optional<LevelSession> const &session)
{
  return session && (!session->deaths.empty() || !session->runs.empty());
}

// Questions that fit where the player is, sent as is when pressed
static std::vector<std::string> quickPrompts(api::context::Scene const &scene)
{
  if (scene.name == "editor")
    return {"Ideas for my level", "How do move triggers work?", "Editor shortcuts"};

  if (scene.name == "level-edit")
    return {"Ideas for my level", "How do I get my level rated?", "What song is this?"};

  if (!scene.level)
    return {"What demon should I beat next?", "Open the #1 demon", "Show trending levels"};

  bool playing = scene.name == "playing";
  auto const &last = SessionStore::last();
  bool struggled = playing ? hasDeaths(SessionStore::current())
                           : hasDeaths(last) && last->isOf(scene.level);

  std::vector<std::string> prompts;
  if (struggled)
    prompts.push_back("Where do I struggle?");
  if (!ProgressStore::history(levelKey(scene.level)).empty())
    prompts.push_back("How is my progress here?");

  prompts.push_back(playing ? "Tips for this level" : "Is this level hard for me?");
  prompts.push_back(playing ? "What song is this?" : "Levels like this one");

  if (prompts.size() > MAX_QUICK_PROMPTS)
    prompts.resize(MAX_QUICK_PROMPTS);
  return prompts;
}

// Color tags like <cy>...</c> are only for the game, not for the clipboard
static std::string stripColorTags(std::string const &text)
{
  static std::regex const tags("</?c[a-z]?>");
  return std::regex_replace(text, tags, "");
}

ChatPopup *ChatPopup::create(std::string const &prompt)
{
  auto ret = new ChatPopup();

  if (ret->init(prompt))
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool ChatPopup::init(std::string const &prompt)
{
  if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

  // ! --- Chat of the open level --- !
  auto scene = api::context::currentScene();
  if (scene.level && Mod::get()->getSettingValue<bool>("level-chats"))
  {
    m_levelChatKey = levelKey(scene.level);
    m_levelName = std::string(scene.level->m_levelName);
    m_chatKey = m_levelChatKey;
  }

  setTitle("AskDash");

  // ! --- Input row --- !
  auto sendSpr = ButtonSprite::create("Send", "goldFont.fnt", "GJ_button_01.png", .8f);
  sendSpr->setScale(.7f);
  m_sendBtn = CCMenuItemSpriteExtra::create(
      sendSpr, this, menu_selector(ChatPopup::onSend));

  float sendWidth = sendSpr->getScaledContentWidth();
  float inputWidth = m_size.width - PADDING * 3 - sendWidth;

  m_input = TextInput::create(inputWidth, "Ask anything...", "chatFont.fnt");
  m_input->setMaxCharCount(MAX_INPUT_LENGTH);
  m_input->setCommonFilter(CommonFilter::Any);
  m_input->setTextAlign(TextInputAlign::Left);
  m_input->setAnchorPoint({0.f, 0.f});
  m_input->setPosition({PADDING, PADDING});
  m_mainLayer->addChild(m_input);

  auto inputMenu = CCMenu::create();
  inputMenu->setPosition({0.f, 0.f});
  m_sendBtn->setPosition({
      m_size.width - PADDING - sendWidth / 2,
      PADDING + m_input->getContentHeight() / 2,
  });
  inputMenu->addChild(m_sendBtn);

  m_spinner = LoadingSpinner::create(20.f);
  m_spinner->setPosition(m_sendBtn->getPosition());
  m_spinner->setVisible(false);
  m_mainLayer->addChild(m_spinner);

  // ! --- Clear button (top right) --- !
  auto clearSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
  clearSpr->setScale(.6f);
  auto clearBtn = CCMenuItemSpriteExtra::create(
      clearSpr, this, menu_selector(ChatPopup::onClear));
  clearBtn->setPosition({m_size.width - 22.f, m_size.height - 22.f});
  inputMenu->addChild(clearBtn);

  // ! --- Plans, settings and status (top) --- !
  auto plansSpr = ButtonSprite::create("Plans", "goldFont.fnt", "GJ_button_04.png", .8f);
  plansSpr->setScale(.5f);
  auto plansBtn = CCMenuItemSpriteExtra::create(
      plansSpr, this, menu_selector(ChatPopup::onPlans));
  plansBtn->setPosition({
      clearBtn->getPositionX() - clearSpr->getScaledContentWidth() / 2 - plansSpr->getScaledContentWidth() / 2 - 4.f,
      clearBtn->getPositionY(),
  });
  inputMenu->addChild(plansBtn);

  auto settingsSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
  settingsSpr->setScale(.45f);
  auto settingsBtn = CCMenuItemSpriteExtra::create(
      settingsSpr, this, menu_selector(ChatPopup::onSettings));
  settingsBtn->setPosition({
      plansBtn->getPositionX() - plansSpr->getScaledContentWidth() / 2 - settingsSpr->getScaledContentWidth() / 2 - 4.f,
      clearBtn->getPositionY(),
  });
  inputMenu->addChild(settingsBtn);

  // ! --- Level / general chat switch (top left) --- !
  if (!m_levelChatKey.empty())
  {
    m_switchSpr = ButtonSprite::create("This level", "goldFont.fnt", "GJ_button_04.png", .8f);
    m_switchSpr->setScale(.5f);
    auto switchBtn = CCMenuItemSpriteExtra::create(
        m_switchSpr, this, menu_selector(ChatPopup::onSwitchChat));
    switchBtn->setPosition({30.f + m_switchSpr->getScaledContentWidth() / 2, clearBtn->getPositionY()});
    inputMenu->addChild(switchBtn);
  }

  m_mainLayer->addChild(inputMenu);

  m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
  m_statusLabel->setScale(.5f);
  m_statusLabel->setOpacity(170);
  m_statusLabel->setPosition({m_size.width / 2, m_size.height - 32.f});
  m_mainLayer->addChild(m_statusLabel);

  // ! --- Quick prompts row above the input --- !
  float promptsY = PADDING * 1.5f + m_input->getContentHeight();
  auto prompts = createQuickPrompts(m_size.width - PADDING * 2);
  prompts->setPosition({m_size.width / 2, promptsY + PROMPTS_HEIGHT / 2});
  m_mainLayer->addChild(prompts);

  // ! --- Messages list --- !
  float listBottom = promptsY + PROMPTS_HEIGHT + PADDING / 2;
  float listTop = m_size.height - 38.f;
  CCSize listSize = {m_size.width - PADDING * 2, listTop - listBottom};

  auto listBg = CCScale9Sprite::create("square02_small.png");
  listBg->setContentSize(listSize);
  listBg->setOpacity(80);
  listBg->setAnchorPoint({0.f, 0.f});
  listBg->setPosition({PADDING, listBottom});
  m_mainLayer->addChild(listBg);

  m_scroll = ScrollLayer::create(listSize);
  m_scroll->setPosition({PADDING, listBottom});
  m_mainLayer->addChild(m_scroll);

  m_emptyNode = createEmptyState();
  m_emptyNode->setPosition({PADDING + listSize.width / 2, listBottom + listSize.height / 2});
  m_mainLayer->addChild(m_emptyNode);

  updateChatLabels();
  rebuildMessages();
  loadStatus();

  if (!prompt.empty())
    sendText(prompt);

  return true;
}

CCNode *ChatPopup::createBubble(ChatMessage const &message, BubbleKind kind)
{
  bool isUser = message.role == "user";
  bool isRich = !isUser && kind == BubbleKind::Normal;
  float listWidth = m_scroll->getContentWidth();
  float maxTextWidth = listWidth * .75f;

  // Streamed text can end inside a color tag, so it stays plain until done
  auto content = message.content.empty() ? std::string("...") : message.content;
  auto text = isRich
                  ? geode::Label::createRich(content, "chatFont.fnt")
                  : geode::Label::create(content, "chatFont.fnt");
  text->setScale(TEXT_SCALE);
  text->setMaxWidth(maxTextWidth / TEXT_SCALE);
  text->setBreakWords(true);
  // isUser ? geode::Label::Alignment::Left : geode::Label::Alignment::Right (::Right doesn't work idk why)
  text->setAlignment(geode::Label::Alignment::Left);

  if (kind == BubbleKind::Error)
    text->setColor({255, 110, 110});

  // ! --- Buttons row under the text --- !
  CCMenu *footer = nullptr;

  if (kind == BubbleKind::Error)
    footer = createActionsMenu({{"retry", "Retry"}});
  else if (isRich && !message.actions.empty())
    footer = createActionsMenu(message.actions);

  auto textSize = text->getScaledContentSize();
  float footerHeight = footer ? footer->getContentHeight() + BUBBLE_PADDING : 0.f;

  CCSize bubbleSize = {
      std::max(textSize.width, footer ? footer->getContentWidth() : 0.f) + BUBBLE_PADDING * 2,
      textSize.height + footerHeight + BUBBLE_PADDING * 2,
  };

  auto bubble = CCNode::create();
  bubble->setContentSize({listWidth, bubbleSize.height});

  auto bg = CCScale9Sprite::create("square02_small.png");
  bg->setContentSize(bubbleSize);
  bg->setOpacity(isUser ? 110 : 60);
  if (isUser)
    bg->setColor({80, 160, 255});

  float bgX = isUser ? listWidth - BUBBLE_PADDING - bubbleSize.width / 2
                     : BUBBLE_PADDING + bubbleSize.width / 2;
  bg->setPosition({bgX, bubbleSize.height / 2});
  bubble->addChild(bg);

  float left = bgX - bubbleSize.width / 2 + BUBBLE_PADDING;

  text->setAnchorPoint({0.f, 1.f});
  text->setPosition({left, bubbleSize.height - BUBBLE_PADDING});
  bubble->addChild(text);

  if (footer)
  {
    footer->setPosition({left, BUBBLE_PADDING});
    bubble->addChild(footer);
  }

  // ! --- Copy button next to the reply --- !
  if (isRich)
  {
    auto copySpr = CCSprite::createWithSpriteFrameName("GJ_copyBtn_001.png");
    copySpr->setScale(COPY_SCALE);

    auto copyBtn = CCMenuItemExt::createSpriteExtra(copySpr, [text = message.content](auto)
                                                    {
      utils::clipboard::write(stripColorTags(text));
      Notification::create("Copied", NotificationIcon::Success)->show(); });

    auto copyMenu = CCMenu::create();
    copyMenu->setPosition({0.f, 0.f});
    copyBtn->setPosition({
        bgX + bubbleSize.width / 2 + copySpr->getScaledContentWidth() / 2 + 4.f,
        bubbleSize.height - copySpr->getScaledContentHeight() / 2,
    });
    copyMenu->addChild(copyBtn);
    bubble->addChild(copyMenu);
  }

  return bubble;
}

CCMenu *ChatPopup::createActionsMenu(std::vector<ChatAction> const &actions)
{
  auto menu = CCMenu::create();
  menu->ignoreAnchorPointForPosition(false);
  menu->setAnchorPoint({0.f, 0.f});

  float x = 0.f;
  float height = 0.f;

  for (auto const &action : actions)
  {
    auto spr = ButtonSprite::create(action.label.c_str(), "goldFont.fnt", "GJ_button_01.png", .8f);
    spr->setScale(ACTION_SCALE);

    auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, action](auto)
                                                {
      if (action.type == "retry")
      {
        if (!m_sending)
          requestReply();
        return;
      }

      // The popup is gone after onClose, keep only the copied action
      onClose(nullptr);
      actions::run(action); });

    auto size = spr->getScaledContentSize();
    btn->setPosition({x + size.width / 2, size.height / 2});
    menu->addChild(btn);

    x += size.width + ACTION_GAP;
    height = std::max(height, size.height);
  }

  menu->setContentSize({x - ACTION_GAP, height});
  return menu;
}

CCNode *ChatPopup::createEmptyState()
{
  auto node = CCNode::create();

  m_emptyLabel = CCLabelBMFont::create("", "bigFont.fnt");
  m_emptyLabel->setScale(.35f);
  m_emptyLabel->setOpacity(120);
  node->addChild(m_emptyLabel);

  return node;
}

CCMenu *ChatPopup::createQuickPrompts(float width)
{
  auto menu = CCMenu::create();
  menu->setContentSize({width, PROMPTS_HEIGHT});
  menu->setLayout(RowLayout::create()->setGap(6.f));

  for (auto const &prompt : quickPrompts(api::context::currentScene()))
  {
    auto spr = ButtonSprite::create(prompt.c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
    spr->setScale(.35f);

    menu->addChild(CCMenuItemExt::createSpriteExtra(spr, [this, prompt](auto)
                                                    { sendText(prompt); }));
  }

  menu->updateLayout();
  return menu;
}

void ChatPopup::rebuildMessages()
{
  m_scroll->m_contentLayer->removeAllChildren();
  m_streamBubble = nullptr;

  for (auto const &message : ChatStore::messages(m_chatKey))
    m_scroll->m_contentLayer->addChild(createBubble(message));

  layoutMessages();
}

void ChatPopup::layoutMessages()
{
  auto content = m_scroll->m_contentLayer;
  auto children = CCArrayExt<CCNode *>(content->getChildren());

  float totalHeight = BUBBLE_GAP;
  for (auto child : children)
    totalHeight += child->getContentHeight() + BUBBLE_GAP;

  float height = std::max(totalHeight, m_scroll->getContentHeight());
  content->setContentSize({m_scroll->getContentWidth(), height});

  float y = height - BUBBLE_GAP;
  for (auto child : children)
  {
    y -= child->getContentHeight();
    child->setPosition({0.f, y});
    y -= BUBBLE_GAP;
  }

  content->setPositionY(0.f);

  m_emptyNode->setVisible(children.size() == 0);
}

void ChatPopup::setSending(bool sending)
{
  m_sending = sending;
  m_sendBtn->setVisible(!sending);
  m_sendBtn->setEnabled(!sending);
  m_spinner->setVisible(sending);
}

void ChatPopup::onSend(CCObject *)
{
  auto text = utils::string::trim(m_input->getString());
  if (text.empty() || m_sending)
    return;

  m_input->setString("");
  m_input->defocus();

  sendText(text);
}

void ChatPopup::sendText(std::string const &text)
{
  if (m_sending)
    return;

  ChatStore::messages(m_chatKey).push_back({"user", text});
  ChatStore::save(m_chatKey);
  requestReply();
}

// ! --- Streamed reply --- !

// Asks for a reply to the history, whose last message is from the user
void ChatPopup::requestReply()
{
  setSending(true);
  rebuildMessages();

  m_streamText.clear();
  m_streamFrom = 0;
  showStreamText();

  m_task.spawn(
      api::chat::startStream(ChatStore::messages(m_chatKey)),
      [this](Result<web::WebResponse> response)
      {
        auto id = api::chat::parseStreamId(response);
        if (id.isErr())
        {
          failReply(id.unwrapErr());
          return;
        }

        m_streamId = id.unwrap();
        pollReply(0.f);
      });
}

void ChatPopup::pollReply(float)
{
  m_task.spawn(
      api::chat::pollStream(m_streamId, m_streamFrom),
      [this](Result<web::WebResponse> response)
      {
        auto result = api::chat::parseStreamChunk(response);
        if (result.isErr())
        {
          failReply(result.unwrapErr());
          return;
        }

        auto chunk = std::move(result).unwrap();
        m_streamFrom = chunk.next;

        if (chunk.reply)
        {
          ChatStore::messages(m_chatKey).push_back(std::move(*chunk.reply));
          ChatStore::save(m_chatKey);
          setSending(false);
          rebuildMessages();
          loadStatus();
          return;
        }

        if (!chunk.delta.empty())
        {
          m_streamText += chunk.delta;
          showStreamText();
        }

        scheduleOnce(schedule_selector(ChatPopup::pollReply), POLL_INTERVAL);
      });
}

void ChatPopup::failReply(billing::ApiError const &error)
{
  log::error("Chat request failed: {}", error.message);

  setSending(false);
  rebuildMessages();

  m_scroll->m_contentLayer->addChild(
      createBubble({"assistant", error.message}, BubbleKind::Error));
  layoutMessages();

  if (error.upgrade)
    onPlans(nullptr);
}

// Replaces the reply bubble at the bottom with the text streamed so far
void ChatPopup::showStreamText()
{
  if (m_streamBubble)
    m_streamBubble->removeFromParent();

  m_streamBubble = createBubble({"assistant", m_streamText}, BubbleKind::Streaming);
  m_scroll->m_contentLayer->addChild(m_streamBubble);
  layoutMessages();
}

void ChatPopup::onClear(CCObject *)
{
  m_task.cancel();
  unschedule(schedule_selector(ChatPopup::pollReply));
  setSending(false);
  ChatStore::messages(m_chatKey).clear();
  ChatStore::save(m_chatKey);
  rebuildMessages();
}

// ! --- Chats --- !

bool ChatPopup::isLevelChat() const
{
  return !m_levelChatKey.empty() && m_chatKey == m_levelChatKey;
}

void ChatPopup::onSwitchChat(CCObject *)
{
  // The reply being written belongs to the shown chat
  if (m_sending)
    return;

  m_chatKey = isLevelChat() ? std::string(ChatStore::GENERAL) : m_levelChatKey;
  updateChatLabels();
  rebuildMessages();
}

void ChatPopup::updateChatLabels()
{
  bool levelChat = isLevelChat();

  m_title->setString(levelChat && !m_levelName.empty() ? m_levelName.c_str() : "AskDash");
  m_title->limitLabelWidth(TITLE_WIDTH, .7f, .3f);

  m_emptyLabel->setString(levelChat ? "Ask me anything about this level!"
                                    : "Ask me anything about Geometry Dash!");

  // Names the chat it switches to
  if (m_switchSpr)
    m_switchSpr->setString(levelChat ? "General" : "This level");
}

void ChatPopup::loadStatus()
{
  m_statusTask.spawn(billing::fetchStatus(), [this](Result<web::WebResponse> response)
                     {
    auto status = billing::parseStatus(response);

    if (status.isErr())
    {
      log::warn("Plan status failed: {}", status.unwrapErr().message);
      m_statusLabel->setString("");

      return;
    }

    m_statusLabel->setString(billing::describe(status.unwrap()).c_str()); });
}

void ChatPopup::onPlans(CCObject *)
{
  auto popup = PlansPopup::create([self = Ref(this)](billing::Status const &status)
                                  { self->m_statusLabel->setString(billing::describe(status).c_str()); });

  if (popup)
    popup->show();
}

void ChatPopup::onSettings(CCObject *)
{
  openSettingsPopup(Mod::get());
}
