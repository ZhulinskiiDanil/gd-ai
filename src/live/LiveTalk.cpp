#include "LiveTalk.hpp"
#include "audio.hpp"
#include "socket.hpp"

#include "../api/context.hpp"
#include "../billing/request.hpp"

#include <Geode/utils/base64.hpp>

using namespace geode::prelude;

static constexpr auto CONSENT_KEY = "live-talk-consent";

// ! --- Session state, main thread only --- !

struct Session
{
  // Bumped on every start and stop, callbacks of an older session are ignored
  int generation = 0;
  live::State state = live::State::Off;
  std::shared_ptr<live::Socket> socket;
  std::shared_ptr<live::Speaker> speaker;
  std::unique_ptr<live::Microphone> microphone;
  std::string startMessage;
  // Error the server sent before closing, shown when the socket closes
  std::string closeReason;
  bool retriedLogin = false;

  std::string subtitle;
  int subtitleId = 0;
  // The current reply is done, the next transcript starts a new subtitle
  bool replyDone = true;
};

// Never destroyed: joining audio threads while the game unloads the mod could hang
static Session &session()
{
  static auto s_session = new Session();
  return *s_session;
}

static void start();

static void runIfCurrent(int generation, std::function<void()> callback)
{
  Loader::get()->queueInMainThread([generation, callback = std::move(callback)]
                                   {
    if (session().generation == generation)
      callback(); });
}

// ! --- Public state --- !

bool live::isSupported()
{
#ifdef GEODE_IS_WINDOWS
  return true;
#else
  return false;
#endif
}

bool live::isActive()
{
  return session().state != State::Off;
}

live::State live::state()
{
  auto &s = session();
  // The reply is done on the server long before it is done playing
  if (s.state == State::Listening && s.speaker && s.speaker->busy())
    return State::Speaking;
  return s.state;
}

std::string const &live::subtitle()
{
  return session().subtitle;
}

int live::subtitleId()
{
  return session().subtitleId;
}

// ! --- Start and stop --- !

static void finish(std::string const &error)
{
  live::stop();
  if (!error.empty())
    Notification::create(error, NotificationIcon::Error, 4.f)->show();
}

static void onServerMessage(std::string const &type, matjson::Value const &json)
{
  auto &s = session();

  if (type == "state")
    s.state = json["state"].asString().unwrapOr("") == "thinking" ? live::State::Thinking : s.state;
  else if (type == "transcript")
  {
    if (s.replyDone)
    {
      s.subtitle.clear();
      s.subtitleId++;
      s.replyDone = false;
    }
    s.subtitle += json["delta"].asString().unwrapOr("");
    s.state = live::State::Listening;
  }
  else if (type == "done")
  {
    s.replyDone = true;
    s.state = live::State::Listening;
  }
  else if (type == "interrupt")
  {
    s.subtitle.clear();
    s.subtitleId++;
    s.replyDone = true;
    s.state = live::State::Listening;
  }
  else if (type == "error")
    s.closeReason = json["error"].asString().unwrapOr("Live talk stopped");
}

static void openMicrophone()
{
  auto &s = session();
  bool headphones = Mod::get()->getSettingValue<bool>("live-talk-headphones");

  s.microphone = live::Microphone::open([socket = s.socket, speaker = s.speaker, headphones](std::vector<int16_t> &chunk)
                                        {
    // Through speakers AskDash would hear itself and stop talking, so it gets silence instead
    if (!headphones && speaker->busy())
      std::fill(chunk.begin(), chunk.end(), int16_t(0));

    auto bytes = std::span(reinterpret_cast<uint8_t const *>(chunk.data()), chunk.size() * sizeof(int16_t));
    socket->send(fmt::format(R"({{"type":"audio","audio":"{}"}})",
                             utils::base64::encode(bytes, utils::base64::Base64Variant::Normal))); });

  if (!s.microphone)
  {
    finish("No microphone found. Plug one in to use live talk");
    return;
  }

  s.state = live::State::Listening;
}

static void connect(int generation, billing::Headers headers)
{
  auto &s = session();

  s.speaker = live::Speaker::open();
  if (!s.speaker)
  {
    finish("No speaker found for live talk");
    return;
  }

  live::SocketHandlers handlers;
  handlers.onOpen = [generation]
  {
    runIfCurrent(generation, []
                 {
      session().socket->send(session().startMessage);
      openMicrophone(); });
  };

  // Voice goes straight to the speaker from the socket thread, everything else to the main thread
  handlers.onMessage = [generation, speaker = s.speaker](std::string message)
  {
    auto const json = matjson::parse(message).unwrapOr(matjson::Value());
    auto type = json["type"].asString().unwrapOr("");

    if (type == "audio")
    {
      if (auto pcm = utils::base64::decode(json["audio"].asString().unwrapOr(""), utils::base64::Base64Variant::Normal))
        speaker->play(std::move(pcm).unwrap());
      return;
    }

    if (type == "interrupt")
      speaker->clear();

    runIfCurrent(generation, [type, json]
                 { onServerMessage(type, json); });
  };

  handlers.onRejected = [generation](int status, std::string body)
  {
    runIfCurrent(generation, [status, body = std::move(body)]
                 {
      auto const json = matjson::parse(body).unwrapOr(matjson::Value());

      // An expired login: log in again once
      if (status == 401 && json["reauth"].asBool().unwrapOr(false) && !session().retriedLogin)
      {
        billing::clearAuth();
        session().retriedLogin = true;
        live::stop();
        start();
        return;
      }

      finish(json["error"].asString().unwrapOr(fmt::format("Live talk failed (HTTP {})", status))); });
  };

  handlers.onClosed = [generation]
  {
    runIfCurrent(generation, []
                 {
      auto reason = session().closeReason;
      finish(reason.empty() ? "Live talk ended" : reason); });
  };

  s.socket = live::Socket::connect(billing::apiUrl("/live"), std::move(headers), std::move(handlers));
  if (!s.socket)
    finish("Live talk is not available on this device");
}

static void start()
{
  auto &s = session();
  int generation = ++s.generation;

  s.state = live::State::Connecting;
  s.closeReason.clear();
  s.subtitle.clear();
  s.replyDone = true;

  auto mod = Mod::get();
  auto start = matjson::makeObject({
      {"type", "start"},
      {"prefs", matjson::makeObject({
                    {"language", mod->getSettingValue<std::string>("live-talk-language")},
                    {"roast", utils::string::toLower(mod->getSettingValue<std::string>("live-talk-roast"))},
                })},
  });
  if (mod->getSettingValue<bool>("send-game-context"))
    start["context"] = api::context::collect();
  s.startMessage = start.dump(matjson::NO_INDENTATION);

  async::spawn(billing::authHeaders(), [generation](Result<billing::Headers> headers)
               {
    if (session().generation != generation)
      return;

    if (headers.isErr())
    {
      finish(headers.unwrapErr());
      return;
    }

    connect(generation, std::move(headers).unwrap()); });
}

void live::stop()
{
  auto &s = session();
  s.generation++;

  if (s.socket)
  {
    s.socket->send(R"({"type":"stop"})");
    s.socket->close();
  }

  // The microphone thread holds the socket, stop it first
  s.microphone.reset();
  s.speaker.reset();
  s.socket.reset();
  s.state = State::Off;
  s.subtitle.clear();
  s.subtitleId++;
}

void live::toggle()
{
  if (!isSupported())
  {
    FLAlertLayer::create("Live talk", "Live talk is only available on <cy>Windows</c> for now.", "OK")->show();
    return;
  }

  if (isActive())
  {
    stop();
    return;
  }

  session().retriedLogin = false;

  if (!PlayLayer::get())
  {
    Notification::create("Start a level to talk with AskDash", NotificationIcon::Info)->show();
    return;
  }

  if (Mod::get()->getSavedValue<bool>(CONSENT_KEY))
  {
    start();
    return;
  }

  if (auto play = PlayLayer::get(); play && !play->m_isPaused)
    play->pauseGame(false);

  createQuickPopup(
      "Live talk",
      "AskDash will <cy>listen to your microphone</c> and talk back while you play. "
      "While it is on, your voice goes to the AskDash server and OpenAI, it is not saved.\n"
      "Use <cg>headphones</c>, or AskDash may hear itself.",
      "Cancel", "Start",
      [](auto, bool yes)
      {
        if (!yes)
          return;
        Mod::get()->setSavedValue(CONSENT_KEY, true);
        start();
      });
}

void live::sendEvent(matjson::Value event)
{
  auto &s = session();
  if (!s.socket || s.state == State::Off || s.state == State::Connecting)
    return;

  s.socket->send(matjson::makeObject({{"type", "event"}, {"event", std::move(event)}}).dump(matjson::NO_INDENTATION));
}
