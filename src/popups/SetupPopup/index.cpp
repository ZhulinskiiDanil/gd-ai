#include "index.hpp"

#include "../SessionPopup/index.hpp"
#include "../../live/LiveTalk.hpp"

#include <Geode/ui/Label.hpp>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

static constexpr auto DONE_KEY = "setup-done";

static constexpr float POPUP_WIDTH = 340.f;
static constexpr float POPUP_HEIGHT = 280.f;
static constexpr float SIDE = 20.f;
static constexpr float HINT_SCALE = .55f;
static constexpr float FOOTER_Y = 24.f;
static constexpr float FOOTER_TOP = 42.f;
static constexpr float OPTION_GAP = 6.f;

// Same as "one-of" of live-talk-language in mod.json
static constexpr char const *LANGUAGES[] = {
    "Auto", "English", "Russian", "Ukrainian", "Spanish", "Portuguese",
    "German", "French", "Polish", "Turkish", "Indonesian", "Vietnamese"};

using Option = SetupPopup::Option;
using Step = SetupPopup::Step;

// ! --- Steps --- !

static Option choice(std::string key, std::string value)
{
  return {
      value,
      [key, value]
      { Mod::get()->setSettingValue<std::string>(key, value); },
      [key, value]
      { return Mod::get()->getSettingValue<std::string>(key) == value; },
  };
}

static Option yesNo(std::string key, bool value)
{
  return {
      value ? "Yes" : "No",
      [key, value]
      { Mod::get()->setSettingValue<bool>(key, value); },
      [key, value]
      { return Mod::get()->getSettingValue<bool>(key) == value; },
  };
}

// The Windows display language, when live talk can speak it
static std::optional<std::string> systemLanguage()
{
#ifdef GEODE_IS_WINDOWS
  switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
  {
  case LANG_ENGLISH: return "English";
  case LANG_RUSSIAN: return "Russian";
  case LANG_UKRAINIAN: return "Ukrainian";
  case LANG_SPANISH: return "Spanish";
  case LANG_PORTUGUESE: return "Portuguese";
  case LANG_GERMAN: return "German";
  case LANG_FRENCH: return "French";
  case LANG_POLISH: return "Polish";
  case LANG_TURKISH: return "Turkish";
  case LANG_INDONESIAN: return "Indonesian";
  case LANG_VIETNAMESE: return "Vietnamese";
  }
#endif
  return std::nullopt;
}

static std::vector<Step> buildSteps()
{
  auto mod = Mod::get();
  std::vector<Step> steps;

  steps.push_back({
      "How good are you at GD?",
      "AskDash tunes its tips to you.",
      {choice("player-skill", "Beginner"), choice("player-skill", "Intermediate"), choice("player-skill", "Pro")},
      3,
  });

  // The other platforms have no live talk
  if (live::isSupported())
  {
    // Preselects the Windows language over the default Auto, also kept when the quiz is skipped
    if (auto language = systemLanguage(); language && mod->getSettingValue<std::string>("live-talk-language") == "Auto")
      mod->setSettingValue<std::string>("live-talk-language", *language);

    std::vector<Option> languages;
    for (auto language : LANGUAGES)
      languages.push_back(choice("live-talk-language", language));

    steps.push_back({
        "What language should AskDash speak?",
        "In live talk AskDash talks with you by voice while you play. Auto answers in the language you speak.",
        std::move(languages),
        3,
    });

    steps.push_back({
        "How hard can AskDash roast you?",
        "Gentle: light teasing. Spicy: friendly roasts. Savage: no mercy. Only your gameplay, never you.",
        {choice("live-talk-roast", "Gentle"), choice("live-talk-roast", "Spicy"), choice("live-talk-roast", "Savage")},
        3,
    });

    steps.push_back({
        "Do you play with headphones?",
        "With headphones you can interrupt AskDash by talking. With speakers it doesn't listen while it talks, "
        "so it won't hear itself.",
        {yesNo("live-talk-headphones", true), yesNo("live-talk-headphones", false)},
        2,
    });
  }

  std::vector<Option> summary = {yesNo("session-summary", true), yesNo("session-summary", false)};
  for (auto &option : summary)
  {
    // Answers the question the first summary would ask
    option.choose = [choose = option.choose]
    {
      choose();
      Mod::get()->setSavedValue(SESSION_SUMMARY_ASKED_KEY, true);
    };
  }

  steps.push_back({
      "Show a summary after each level?",
      "Your attempts, best percent and where you died, when you leave a level.",
      std::move(summary),
      2,
  });

  return steps;
}

// ! --- Popup --- !

bool SetupPopup::shouldShow()
{
  return !Mod::get()->getSavedValue<bool>(DONE_KEY);
}

SetupPopup *SetupPopup::create()
{
  auto ret = new SetupPopup();

  if (ret->init())
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool SetupPopup::init()
{
  if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

  setTitle("Set up AskDash");
  m_steps = buildSteps();
  showStep(0);
  return true;
}

// Button callbacks rebuild the step on the next frame: the pressed button's menu is removed with it
static void later(CCNode *node, std::function<void()> callback)
{
  node->runAction(CallFuncExt::create(std::move(callback)));
}

void SetupPopup::showStep(size_t index)
{
  m_step = index;

  if (m_content)
    m_content->removeFromParent();
  m_content = CCNode::create();
  m_mainLayer->addChild(m_content);

  auto const &step = m_steps[index];
  float hintBottom = addHeader(fmt::format("Question {} of {}", index + 1, m_steps.size()), step.question, step.hint);
  addOptions(step, hintBottom);
  addFooter();
}

void SetupPopup::showDone()
{
  m_content->removeFromParent();
  m_content = CCNode::create();
  m_mainLayer->addChild(m_content);

  float hintBottom = addHeader(
      "",
      "You're all set!",
      live::isSupported()
          ? "In a level, press Ctrl+Shift+L or LIVE in the pause menu to talk with AskDash by voice. "
            "You can change all of this in the mod settings."
          : "You can change all of this in the mod settings.");

  auto menu = CCMenu::create();
  menu->setPosition({0.f, 0.f});
  m_content->addChild(menu);

  auto spr = ButtonSprite::create("Let's go!", "goldFont.fnt", "GJ_button_01.png", .8f);
  auto btn = CCMenuItemExt::createSpriteExtra(spr, [this](auto)
                                              { onClose(nullptr); });
  btn->setPosition({m_size.width / 2, (FOOTER_TOP + hintBottom) / 2});
  menu->addChild(btn);
}

float SetupPopup::addHeader(std::string const &caption, std::string const &question, std::string const &hint)
{
  float top = m_size.height;

  if (!caption.empty())
  {
    auto captionLabel = CCLabelBMFont::create(caption.c_str(), "chatFont.fnt");
    captionLabel->setScale(.5f);
    captionLabel->setOpacity(170);
    captionLabel->setPosition({m_size.width / 2, top - 42.f});
    m_content->addChild(captionLabel);
  }

  auto questionLabel = CCLabelBMFont::create(question.c_str(), "goldFont.fnt");
  questionLabel->limitLabelWidth(m_size.width - SIDE * 2, .7f, .3f);
  questionLabel->setPosition({m_size.width / 2, top - 62.f});
  m_content->addChild(questionLabel);

  auto hintLabel = geode::Label::create(hint.c_str(), "chatFont.fnt");
  hintLabel->setScale(HINT_SCALE);
  hintLabel->setMaxWidth((m_size.width - SIDE * 2 - 10.f) / HINT_SCALE);
  hintLabel->setAlignment(geode::Label::Alignment::Center);
  hintLabel->setAnchorPoint({.5f, 1.f});
  hintLabel->setPosition({m_size.width / 2, top - 76.f});
  hintLabel->setOpacity(210);
  m_content->addChild(hintLabel);

  return hintLabel->getPositionY() - hintLabel->getScaledContentHeight();
}

// A grid of answers between the hint and the footer, the picked one is green
void SetupPopup::addOptions(Step const &step, float top)
{
  int count = static_cast<int>(step.options.size());
  int columns = std::max(1, step.columns);
  int rows = (count + columns - 1) / columns;

  float width = columns == 1 ? 180.f : columns == 2 ? 110.f : 96.f;
  float height = rows > 2 ? 22.f : 30.f;
  float gridHeight = rows * height + (rows - 1) * OPTION_GAP;
  float centerY = (FOOTER_TOP + top - OPTION_GAP) / 2;

  auto menu = CCMenu::create();
  menu->setPosition({0.f, 0.f});
  m_content->addChild(menu);

  for (int i = 0; i < count; i++)
  {
    auto const &option = step.options[i];
    int row = i / columns;
    int column = i % columns;

    // A last row that isn't full is centered
    int inRow = std::min(columns, count - row * columns);
    float rowWidth = inRow * width + (inRow - 1) * OPTION_GAP;

    auto spr = ButtonSprite::create(
        option.label.c_str(), static_cast<int>(width), true, "bigFont.fnt",
        option.isSelected() ? "GJ_button_01.png" : "GJ_button_04.png", height, .5f);
    spr->m_label->limitLabelWidth(width - 12.f, .5f, .1f);

    auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, i](auto)
                                                {
      m_steps[m_step].options[i].choose();
      later(this, [this]
            {
        if (m_step + 1 < m_steps.size())
          showStep(m_step + 1);
        else
          showDone(); }); });
    btn->setPosition({
        m_size.width / 2 - rowWidth / 2 + width / 2 + column * (width + OPTION_GAP),
        centerY + gridHeight / 2 - height / 2 - row * (height + OPTION_GAP),
    });
    menu->addChild(btn);
  }
}

void SetupPopup::addFooter()
{
  auto menu = CCMenu::create();
  menu->setPosition({0.f, 0.f});
  m_content->addChild(menu);

  if (m_step > 0)
  {
    auto backSpr = ButtonSprite::create("Back", "goldFont.fnt", "GJ_button_04.png", .6f);
    auto backBtn = CCMenuItemExt::createSpriteExtra(backSpr, [this](auto)
                                                    { later(this, [this]
                                                            { showStep(m_step - 1); }); });
    backBtn->setPosition({SIDE + backSpr->getScaledContentWidth() / 2, FOOTER_Y});
    menu->addChild(backBtn);
  }

  auto skipSpr = ButtonSprite::create("Skip", "goldFont.fnt", "GJ_button_04.png", .6f);
  auto skipBtn = CCMenuItemExt::createSpriteExtra(skipSpr, [this](auto)
                                                  { onClose(nullptr); });
  skipBtn->setPosition({m_size.width - SIDE - skipSpr->getScaledContentWidth() / 2, FOOTER_Y});
  menu->addChild(skipBtn);
}

// Shown once: finishing, skipping and closing all count
void SetupPopup::onClose(CCObject *sender)
{
  Mod::get()->setSavedValue(DONE_KEY, true);
  Popup::onClose(sender);
}
