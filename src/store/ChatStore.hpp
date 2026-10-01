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
  std::vector<ChatMessage> &messages();

  // Call after changing messages()
  void save();
}
