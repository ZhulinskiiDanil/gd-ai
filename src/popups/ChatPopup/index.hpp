#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../../store/ChatStore.hpp"

using namespace geode::prelude;

class ChatPopup : public geode::Popup
{
private:
  ScrollLayer *m_scroll = nullptr;
  TextInput *m_input = nullptr;
  CCMenuItemSpriteExtra *m_sendBtn = nullptr;
  LoadingSpinner *m_spinner = nullptr;
  CCLabelBMFont *m_emptyLabel = nullptr;

  // Plan and usage left / "Free: 40% left today"
  CCLabelBMFont *m_statusLabel = nullptr;

  async::TaskHolder<Result<web::WebResponse>> m_task;
  async::TaskHolder<Result<web::WebResponse>> m_statusTask;
  bool m_sending = false;

  bool init();

  void onSend(CCObject *);
  void onClear(CCObject *);
  void onPlans(CCObject *);
  void onSettings(CCObject *);

  void loadStatus();

  void setSending(bool sending);

  void rebuildMessages();
  CCNode *createBubble(ChatMessage const &message, bool isError = false);
  void addErrorBubble(std::string const &text);
  void layoutMessages();

public:
  static ChatPopup *create();
};
