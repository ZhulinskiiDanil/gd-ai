#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../../billing/api.hpp"

using namespace geode::prelude;

class PlansPopup : public geode::Popup
{
private:
  using Task = async::TaskHolder<Result<web::WebResponse>>;

  std::function<void(billing::Status const &)> m_onStatus;

  CCLabelBMFont *m_statusLabel = nullptr;
  CCNode *m_plansNode = nullptr;
  TextInput *m_keyInput = nullptr;
  CCMenuItemSpriteExtra *m_activateBtn = nullptr;
  CCMenuItemSpriteExtra *m_buyBtn = nullptr;
  std::string m_buyUrl;

  Task m_statusTask;
  Task m_plansTask;
  Task m_activateTask;

  bool init(std::function<void(billing::Status const &)> onStatus);

  void loadStatus();
  void loadPlans();
  void showStatus(billing::Status const &status);
  void showPlans(billing::Plans const &plans);

  void onBuy(CCObject *);
  void onActivate(CCObject *);

public:
  static PlansPopup *create(std::function<void(billing::Status const &)> onStatus);
};
