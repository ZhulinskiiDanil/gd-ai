#pragma once

#include <string>
#include <vector>
#include <Geode/Geode.hpp>

// Button under an assistant reply, matches Action in gd-ai-server src/tools.ts
struct ChatAction
{
  // "open_level" | "open_search" | "open_browser" | ...
  std::string type;
  std::string label;
  // The whole action object, fields depend on type
  matjson::Value data;
};

struct ChatMessage
{
  // "user" | "assistant"
  std::string role;
  std::string content;
  std::vector<ChatAction> actions = {};
};

// Kept on disk when "save-history" is on, otherwise until user leave gd
namespace ChatStore
{
  // Key of the chat not tied to a level, others use levelKey()
  inline constexpr auto GENERAL = "general";

  // Messages of a chat, empty if it has none yet
  std::vector<ChatMessage> &messages(std::string const &key);

  // Call after changing messages(key)
  void save(std::string const &key);
}
