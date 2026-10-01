#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

// Short quiz on the first launch with the mod: skill, live talk language and roast level, headphones,
// session summaries. Every answer goes to the mod settings right away, closing it early keeps the rest
class SetupPopup : public geode::Popup
{
public:
  struct Option
  {
    std::string label;
    std::function<void()> choose;
    std::function<bool()> isSelected;
  };

  struct Step
  {
    std::string question;
    std::string hint;
    std::vector<Option> options;
    int columns = 1;
  };

private:
  std::vector<Step> m_steps;
  size_t m_step = 0;
  // Everything of the current step, rebuilt on every step
  CCNode *m_content = nullptr;

  bool init() override;

  void showStep(size_t index);
  void showDone();
  // Returns where the hint ends
  float addHeader(std::string const &caption, std::string const &question, std::string const &hint);
  void addOptions(Step const &step, float top);
  void addFooter();

  void onClose(CCObject *) override;

public:
  // Not finished or closed before
  static bool shouldShow();
  static SetupPopup *create();
};
