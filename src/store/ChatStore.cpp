#include "ChatStore.hpp"

#include <ctime>
#include <map>

using namespace geode::prelude;

static constexpr size_t MAX_SAVED_MESSAGES = 50;
static constexpr size_t MAX_SAVED_CHATS = 30;
static constexpr auto SAVE_KEY = "chats";
// Before 1.6.0 there was only one chat, saved as an array
static constexpr auto OLD_SAVE_KEY = "history";

struct Chat
{
  // Unix seconds, the least recent chats are dropped first
  int64_t updatedAt = 0;
  std::vector<ChatMessage> messages;
};

static bool isSavingEnabled()
{
  return Mod::get()->getSettingValue<bool>("save-history");
}

static std::vector<ChatMessage> messagesFromJson(matjson::Value const &json)
{
  std::vector<ChatMessage> messages;
  auto items = json.asArray();
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

static matjson::Value messagesToJson(std::vector<ChatMessage> const &messages)
{
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

  return json;
}

static std::map<std::string, Chat> load()
{
  std::map<std::string, Chat> chats;
  if (!isSavingEnabled())
    return chats;

  // The old single chat becomes the general one, written right away so it isn't lost
  auto mod = Mod::get();
  if (mod->hasSavedValue(OLD_SAVE_KEY))
  {
    auto old = mod->getSavedValue<matjson::Value>(OLD_SAVE_KEY);
    if (!mod->hasSavedValue(SAVE_KEY) && old.isArray() && old.size() > 0)
      mod->setSavedValue(SAVE_KEY, matjson::makeObject({
                                       {ChatStore::GENERAL, matjson::makeObject({
                                                                {"updatedAt", static_cast<int64_t>(std::time(nullptr))},
                                                                {"messages", old},
                                                            })},
                                   }));
    mod->getSaveContainer().erase(OLD_SAVE_KEY);
  }

  auto saved = mod->getSavedValue<matjson::Value>(SAVE_KEY);
  if (!saved.isObject())
    return chats;

  for (auto const &item : saved)
  {
    auto key = item.getKey();
    if (!key)
      continue;

    chats[*key] = {
        item["updatedAt"].as<int64_t>().unwrapOr(0),
        messagesFromJson(item["messages"]),
    };
  }

  return chats;
}

static std::map<std::string, Chat> &chats()
{
  static std::map<std::string, Chat> s_chats = load();
  return s_chats;
}

// Keeps the general chat and the most recent level chats
static void pruneChats()
{
  auto &all = chats();
  while (all.size() > MAX_SAVED_CHATS)
  {
    auto oldest = all.end();
    for (auto it = all.begin(); it != all.end(); ++it)
    {
      if (it->first != ChatStore::GENERAL && (oldest == all.end() || it->second.updatedAt < oldest->second.updatedAt))
        oldest = it;
    }

    if (oldest == all.end())
      return;
    all.erase(oldest);
  }
}

std::vector<ChatMessage> &ChatStore::messages(std::string const &key)
{
  return chats()[key].messages;
}

void ChatStore::save(std::string const &key)
{
  chats()[key].updatedAt = std::time(nullptr);
  pruneChats();

  if (!isSavingEnabled())
  {
    Mod::get()->setSavedValue(SAVE_KEY, matjson::makeObject({}));
    return;
  }

  auto json = matjson::makeObject({});
  for (auto const &[chatKey, chat] : chats())
  {
    if (chat.messages.empty())
      continue;

    json[chatKey] = matjson::makeObject({
        {"updatedAt", chat.updatedAt},
        {"messages", messagesToJson(chat.messages)},
    });
  }

  Mod::get()->setSavedValue(SAVE_KEY, json);
}

// Turning saving off deletes what is already on disk
$execute
{
  listenForSettingChanges<bool>("save-history", [](bool enabled)
                                {
    if (!enabled)
      Mod::get()->setSavedValue(SAVE_KEY, matjson::makeObject({})); });
}
