#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Microphone and speaker in the format of the Realtime API: PCM16 mono 24 kHz. Windows only (winmm)
namespace live
{
  inline constexpr int SAMPLE_RATE = 24000;

  class Microphone
  {
  public:
    virtual ~Microphone() = default;

    // Chunks of ~100 ms, called on the microphone's own thread; nullptr when there is no microphone
    static std::unique_ptr<Microphone> open(std::function<void(std::vector<int16_t> &)> onChunk);
  };

  class Speaker
  {
  public:
    virtual ~Speaker() = default;

    // Thread safe
    virtual void play(std::vector<uint8_t> pcm) = 0;
    // Drops everything queued, when the player interrupts
    virtual void clear() = 0;
    // Still has sound to play
    virtual bool busy() const = 0;

    static std::unique_ptr<Speaker> open();
  };
}
