#include "audio.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

#include <array>
#include <atomic>
#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <windows.h>
#include <mmsystem.h>

using namespace geode::prelude;

static constexpr DWORD CHUNK_SAMPLES = live::SAMPLE_RATE / 10;
static constexpr size_t MIC_BUFFERS = 4;
// Speaker buffers handed to the driver at once, the rest waits in the queue
static constexpr size_t MAX_QUEUED_BUFFERS = 16;

static WAVEFORMATEX pcmFormat()
{
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = live::SAMPLE_RATE;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 2;
  format.nAvgBytesPerSec = live::SAMPLE_RATE * 2;
  return format;
}

// ! --- Microphone --- !

class WinMicrophone final : public live::Microphone
{
  HWAVEIN m_device = nullptr;
  HANDLE m_event = nullptr;
  std::array<WAVEHDR, MIC_BUFFERS> m_headers{};
  std::array<std::vector<int16_t>, MIC_BUFFERS> m_buffers;
  std::function<void(std::vector<int16_t> &)> m_onChunk;
  std::atomic<bool> m_running = false;
  std::thread m_thread;

public:
  explicit WinMicrophone(std::function<void(std::vector<int16_t> &)> onChunk) : m_onChunk(std::move(onChunk)) {}

  bool start()
  {
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto format = pcmFormat();

    // WAVE_MAPPER converts from whatever the default microphone records
    if (auto error = waveInOpen(&m_device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(m_event), 0, CALLBACK_EVENT))
    {
      log::warn("Live talk: no microphone ({})", error);
      m_device = nullptr;
      return false;
    }

    for (size_t i = 0; i < MIC_BUFFERS; i++)
    {
      m_buffers[i].resize(CHUNK_SAMPLES);
      m_headers[i].lpData = reinterpret_cast<LPSTR>(m_buffers[i].data());
      m_headers[i].dwBufferLength = CHUNK_SAMPLES * sizeof(int16_t);
      waveInPrepareHeader(m_device, &m_headers[i], sizeof(WAVEHDR));
      waveInAddBuffer(m_device, &m_headers[i], sizeof(WAVEHDR));
    }

    m_running = true;
    m_thread = std::thread([this]
                           { loop(); });
    waveInStart(m_device);
    return true;
  }

  ~WinMicrophone() override
  {
    m_running = false;
    if (m_event)
      SetEvent(m_event);
    if (m_thread.joinable())
      m_thread.join();

    if (m_device)
    {
      waveInReset(m_device);
      for (auto &header : m_headers)
        waveInUnprepareHeader(m_device, &header, sizeof(WAVEHDR));
      waveInClose(m_device);
    }
    if (m_event)
      CloseHandle(m_event);
  }

private:
  void loop()
  {
    std::vector<int16_t> chunk;
    while (m_running)
    {
      WaitForSingleObject(m_event, 200);

      for (size_t i = 0; i < MIC_BUFFERS && m_running; i++)
      {
        auto &header = m_headers[i];
        if (!(header.dwFlags & WHDR_DONE))
          continue;

        chunk.assign(m_buffers[i].begin(), m_buffers[i].begin() + header.dwBytesRecorded / sizeof(int16_t));
        waveInAddBuffer(m_device, &header, sizeof(WAVEHDR));

        if (!chunk.empty())
          m_onChunk(chunk);
      }
    }
  }
};

std::unique_ptr<live::Microphone> live::Microphone::open(std::function<void(std::vector<int16_t> &)> onChunk)
{
  auto mic = std::make_unique<WinMicrophone>(std::move(onChunk));
  if (!mic->start())
    return nullptr;
  return mic;
}

// ! --- Speaker --- !

class WinSpeaker final : public live::Speaker
{
  struct Buffer
  {
    WAVEHDR header{};
    std::vector<uint8_t> pcm;
  };

  HWAVEOUT m_device = nullptr;
  HANDLE m_event = nullptr;
  // Guards the queue, the buffers and every waveOut call
  mutable std::mutex m_mutex;
  std::deque<std::vector<uint8_t>> m_queue;
  std::list<Buffer> m_playing;
  std::atomic<bool> m_running = false;
  std::thread m_thread;

public:
  bool start()
  {
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto format = pcmFormat();

    if (auto error = waveOutOpen(&m_device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(m_event), 0, CALLBACK_EVENT))
    {
      log::warn("Live talk: no speaker ({})", error);
      m_device = nullptr;
      return false;
    }

    m_running = true;
    m_thread = std::thread([this]
                           { loop(); });
    return true;
  }

  ~WinSpeaker() override
  {
    m_running = false;
    if (m_event)
      SetEvent(m_event);
    if (m_thread.joinable())
      m_thread.join();

    if (m_device)
    {
      waveOutReset(m_device);
      for (auto &buffer : m_playing)
        waveOutUnprepareHeader(m_device, &buffer.header, sizeof(WAVEHDR));
      waveOutClose(m_device);
    }
    if (m_event)
      CloseHandle(m_event);
  }

  void play(std::vector<uint8_t> pcm) override
  {
    {
      std::lock_guard lock(m_mutex);
      m_queue.push_back(std::move(pcm));
    }
    SetEvent(m_event);
  }

  void clear() override
  {
    std::lock_guard lock(m_mutex);
    m_queue.clear();
    // Marks every playing buffer done, the loop frees them
    waveOutReset(m_device);
  }

  bool busy() const override
  {
    std::lock_guard lock(m_mutex);
    return !m_queue.empty() || !m_playing.empty();
  }

private:
  void loop()
  {
    while (m_running)
    {
      WaitForSingleObject(m_event, 100);
      std::lock_guard lock(m_mutex);

      for (auto it = m_playing.begin(); it != m_playing.end();)
      {
        if (it->header.dwFlags & WHDR_DONE)
        {
          waveOutUnprepareHeader(m_device, &it->header, sizeof(WAVEHDR));
          it = m_playing.erase(it);
        }
        else
          ++it;
      }

      while (!m_queue.empty() && m_playing.size() < MAX_QUEUED_BUFFERS)
      {
        auto &buffer = m_playing.emplace_back();
        buffer.pcm = std::move(m_queue.front());
        m_queue.pop_front();

        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.pcm.data());
        buffer.header.dwBufferLength = static_cast<DWORD>(buffer.pcm.size());
        waveOutPrepareHeader(m_device, &buffer.header, sizeof(WAVEHDR));
        waveOutWrite(m_device, &buffer.header, sizeof(WAVEHDR));
      }
    }
  }
};

std::unique_ptr<live::Speaker> live::Speaker::open()
{
  auto speaker = std::make_unique<WinSpeaker>();
  if (!speaker->start())
    return nullptr;
  return speaker;
}

#else

std::unique_ptr<live::Microphone> live::Microphone::open(std::function<void(std::vector<int16_t> &)>)
{
  return nullptr;
}

std::unique_ptr<live::Speaker> live::Speaker::open()
{
  return nullptr;
}

#endif
