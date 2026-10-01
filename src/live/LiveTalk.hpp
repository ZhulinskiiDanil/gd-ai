#pragma once

#include <string>
#include <Geode/Geode.hpp>

// Live talk: AskDash listens to the microphone and talks back by voice while a level is played.
// Windows only, elsewhere toggle() explains that. Everything here is for the main thread
namespace live
{
  enum class State
  {
    Off,
    Connecting,
    Listening,
    Thinking,
    Speaking,
  };

  bool isSupported();
  bool isActive();
  State state();

  // What AskDash is saying, changes id with every new reply
  std::string const &subtitle();
  int subtitleId();

  // Starts (asking for consent the first time) or stops live talk
  void toggle();
  void stop();

  // A game event for the model to react to, see gameEventSchema in gd-ai-server src/live.ts
  void sendEvent(matjson::Value event);
}
