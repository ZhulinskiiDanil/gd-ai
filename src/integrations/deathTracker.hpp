#pragma once

#include <Geode/Geode.hpp>

// Optional Death Tracker (elohmrow.death_tracker) integration.
// It has no API, so this reads its save files: {save-path-new}/{levelKey}/general.dt
namespace integrations::deathTracker
{
  // Deaths and runs of the level, nullopt when Death Tracker is off or has no stats for it
  std::optional<matjson::Value> collect(GJGameLevel *level);
}
