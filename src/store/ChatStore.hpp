#pragma once

#include <string>
#include <vector>

struct ChatMessage
{
  // "user" | "assistant"
  std::string role;
  std::string content;
};

// live until user leave gd
namespace ChatStore
{
  std::vector<ChatMessage> &messages();
}
