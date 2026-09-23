#include "ChatStore.hpp"

std::vector<ChatMessage> &ChatStore::messages()
{
  static std::vector<ChatMessage> s_messages;
  return s_messages;
}
