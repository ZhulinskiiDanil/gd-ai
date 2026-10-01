#include "ChatStore.hpp"

using namespace geode::prelude;

static constexpr size_t MAX_SAVED_MESSAGES = 50;
static constexpr auto SAVE_KEY = "history";

static bool isSavingEnabled()
{
  return Mod::get()->getSettingValue<bool>("save-history");
}

static std::vector<ChatMessage> load()
{
  std::vector<ChatMessage> messages;
  if (!isSavingEnabled())
    return messages;

  auto saved = Mod::get()->getSavedValue<matjson::Value>(SAVE_KEY);
  auto items = saved.asArray();
  if (items.isErr())
    return messages;

  for (auto const &item : items.unwrap())
  {
    ChatMessage message{
        item["role"].asString().unwrapOr(""),
        item["content"].asString().unwrapOr(""),
    };
    if (message.role.empty() || message.content.empty())
      continue;

    if (auto actions = item["actions"].asArray())
    {
      for (auto const &action : actions.unwrap())
        message.actions.push_back({
            action["type"].asString().unwrapOr(""),
            action["label"].asString().unwrapOr(""),
            action,
        });
    }

    messages.push_back(std::move(message));
  }

  return messages;
}

std::vector<ChatMessage> &ChatStore::messages()
{
  static std::vector<ChatMessage> s_messages = load();
  return s_messages;
}

void ChatStore::save()
{
  if (!isSavingEnabled())
  {
    Mod::get()->setSavedValue(SAVE_KEY, matjson::Value::array());
    return;
  }

  auto const &messages = ChatStore::messages();
  auto begin = messages.size() > MAX_SAVED_MESSAGES
                   ? messages.end() - MAX_SAVED_MESSAGES
                   : messages.begin();

  auto json = matjson::Value::array();
  for (auto it = begin; it != messages.end(); ++it)
  {
    auto actions = matjson::Value::array();
    for (auto const &action : it->actions)
      actions.push(action.data);

    json.push(matjson::makeObject({
        {"role", it->role},
        {"content", it->content},
        {"actions", actions},
    }));
  }

  Mod::get()->setSavedValue(SAVE_KEY, json);
}

// Turning saving off deletes what is already on disk
$execute
{
  listenForSettingChanges<bool>("save-history", [](bool enabled)
                                {
    if (!enabled)
      Mod::get()->setSavedValue(SAVE_KEY, matjson::Value::array()); });
}
