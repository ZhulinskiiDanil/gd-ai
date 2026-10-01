#pragma once

#include "../store/ChatStore.hpp"

namespace actions
{
  // Opens the level, list or profile from an AI reply button
  void run(ChatAction const &action);
}
