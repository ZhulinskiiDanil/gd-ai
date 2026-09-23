#include "index.hpp"

#include "../../api/chat.hpp"

#include <Geode/ui/Label.hpp>

static constexpr float POPUP_WIDTH = 400.f;
static constexpr float POPUP_HEIGHT = 270.f;
static constexpr float PADDING = 10.f;
static constexpr float BUBBLE_PADDING = 6.f;
static constexpr float BUBBLE_GAP = 5.f;
static constexpr float TEXT_SCALE = .6f;
static constexpr size_t MAX_INPUT_LENGTH = 500;

ChatPopup *ChatPopup::create()
{
  auto ret = new ChatPopup();

  if (ret->init())
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool ChatPopup::init()
{
  if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

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

  m_mainLayer->addChild(inputMenu);

  // ! --- Messages list --- !
  float listBottom = PADDING * 2 + m_input->getContentHeight();
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

  m_emptyLabel = CCLabelBMFont::create("Ask me anything about Geometry Dash!", "bigFont.fnt");
  m_emptyLabel->setScale(.35f);
  m_emptyLabel->setOpacity(120);
  m_emptyLabel->setPosition({PADDING + listSize.width / 2, listBottom + listSize.height / 2});
  m_mainLayer->addChild(m_emptyLabel);

  rebuildMessages();

  return true;
}

CCNode *ChatPopup::createBubble(ChatMessage const &message, bool isError)
{
  bool isUser = message.role == "user";
  float listWidth = m_scroll->getContentWidth();
  float maxTextWidth = listWidth * .75f;

  auto text = isUser || isError
                  ? geode::Label::create(message.content, "chatFont.fnt")
                  : geode::Label::createRich(message.content, "chatFont.fnt");
  text->setScale(TEXT_SCALE);
  text->setMaxWidth(maxTextWidth / TEXT_SCALE);
  text->setBreakWords(true);
  // isUser ? geode::Label::Alignment::Left : geode::Label::Alignment::Right (::Right doesn't work idk why)
  text->setAlignment(geode::Label::Alignment::Left);

  if (isError)
    text->setColor({255, 110, 110});

  auto textSize = text->getScaledContentSize();
  CCSize bubbleSize = {
      textSize.width + BUBBLE_PADDING * 2,
      textSize.height + BUBBLE_PADDING * 2,
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

  text->setAnchorPoint({.5f, .5f});
  text->setPosition(bg->getPosition());
  bubble->addChild(text);

  return bubble;
}

void ChatPopup::rebuildMessages()
{
  m_scroll->m_contentLayer->removeAllChildren();

  for (auto const &message : ChatStore::messages())
    m_scroll->m_contentLayer->addChild(createBubble(message));

  layoutMessages();
}

void ChatPopup::addErrorBubble(std::string const &text)
{
  m_scroll->m_contentLayer->addChild(
      createBubble({"assistant", text}, true));

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

  m_emptyLabel->setVisible(children.size() == 0);
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
  if (m_sending)
    return;

  auto text = utils::string::trim(m_input->getString());
  if (text.empty())
    return;

  m_input->setString("");
  m_input->defocus();

  ChatStore::messages().push_back({"user", text});
  rebuildMessages();
  setSending(true);

  m_task.spawn(
      api::chat::sendMessages(ChatStore::messages()),
      [this](web::WebResponse response)
      {
        setSending(false);

        auto result = api::chat::parseReply(response);

        if (result.isErr())
        {
          log::error("Chat request failed: {}", result.unwrapErr());
          addErrorBubble(result.unwrapErr());
          return;
        }

        ChatStore::messages().push_back({"assistant", result.unwrap()});
        rebuildMessages();
      });
}

void ChatPopup::onClear(CCObject *)
{
  m_task.cancel();
  setSending(false);
  ChatStore::messages().clear();
  rebuildMessages();
}
