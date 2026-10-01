#pragma once

#include <Geode/Geode.hpp>

namespace api::context
{
  struct Scene
  {
    // "menu" | "level-info" | "level-edit" | "playing" | "editor", matches gd-ai-server src/context.ts
    std::string name;
    GJGameLevel *level = nullptr;
  };

  // Where the player is right now and the level open there
  Scene currentScene();

  matjson::Value collect();
}
