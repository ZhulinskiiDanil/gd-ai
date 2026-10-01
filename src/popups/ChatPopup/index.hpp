#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../../billing/api.hpp"
#include "../../store/ChatStore.hpp"

using namespace geode::prelude;

class ChatPopup : public geode::Popup
{
private:
  enum class BubbleKind
  {
    Normal,
    // Reply still being generated, plain text without buttons
    Streaming,
    // Failed request, with a Retry button
    Error,
  };

  ScrollLayer *m_scroll = nullptr;
  TextInput *m_input = nullptr;
  CCMenuItemSpriteExtra *m_sendBtn = nullptr;
  LoadingSpinner *m_spinner = nullptr;
  CCNode *m_emptyNode = nullptr;
  CCLabelBMFont *m_emptyLabel = nullptr;

  // ! --- Chats --- !
  // ChatStore key of the shown chat
  std::string m_chatKey = ChatStore::GENERAL;
  // Chat of the open level, empty in menus or with "level-chats" off
  std::string m_levelChatKey;
  std::string m_levelName;
  ButtonSprite *m_switchSpr = nullptr;

  // Plan and usage left / "Free: 40% left today"
  CCLabelBMFont *m_statusLabel = nullptr;

  async::TaskHolder<Result<web::WebResponse>> m_task;
  async::TaskHolder<Result<web::WebResponse>> m_statusTask;
  bool m_sending = false;

  // ! --- Streamed reply --- !
  std::string m_streamId;
  std::string m_streamText;
  size_t m_streamFrom = 0;
  CCNode *m_streamBubble = nullptr;

  bool init(std::string const &prompt);

  void onSend(CCObject *);
  void onClear(CCObject *);
  void onPlans(CCObject *);
  void onSettings(CCObject *);
  void onSwitchChat(CCObject *);

  bool isLevelChat() const;
  void updateChatLabels();

  void loadStatus();
  void sendText(std::string const &text);

  void requestReply();
  void pollReply(float);
  void failReply(billing::ApiError const &error);
  void showStreamText();

  void setSending(bool sending);

  void rebuildMessages();
  CCNode *createBubble(ChatMessage const &message, BubbleKind kind = BubbleKind::Normal);
  CCMenu *createActionsMenu(std::vector<ChatAction> const &actions);
  CCNode *createEmptyState();
  CCMenu *createQuickPrompts(float width);
  void layoutMessages();

public:
  // Sends `prompt` right away when it is not empty
  static ChatPopup *create(std::string const &prompt = "");
};
