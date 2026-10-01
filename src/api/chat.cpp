#include "chat.hpp"
#include "context.hpp"

#include <chrono>

using namespace geode::prelude;

static constexpr size_t MAX_HISTORY = 20;

billing::ResponseFuture api::chat::startStream(
    std::vector<ChatMessage> const &messages)
{
  auto begin = messages.size() > MAX_HISTORY
                   ? messages.end() - MAX_HISTORY
                   : messages.begin();

  auto jsonMessages = matjson::Value::array();

  for (auto it = begin; it != messages.end(); ++it)
  {
    jsonMessages.push(matjson::makeObject({
        {"role", it->role},
        {"content", it->content},
    }));
  }

  auto body = matjson::makeObject({{"messages", jsonMessages}});
  if (Mod::get()->getSettingValue<bool>("send-game-context"))
    body["context"] = api::context::collect();

  // Built again if the login has to be refreshed
  return billing::send("POST", billing::apiUrl("/chat/stream"), [body = std::move(body)]
                       {
    web::WebRequest request;
    request.header("Content-Type", "application/json");
    request.bodyJSON(body);
    request.timeout(std::chrono::seconds(15));
    return request; });
}

billing::ApiResult<std::string> api::chat::parseStreamId(
    Result<web::WebResponse> const &response)
{
  GEODE_UNWRAP_INTO(auto json, billing::parseJson(response));

  auto id = json["id"].asString();
  if (id.isErr())
    return Err(billing::ApiError{"Invalid server response"});

  return Ok(id.unwrap());
}

// The stream id is enough, polls don't need the GD login
billing::ResponseFuture api::chat::pollStream(std::string id, size_t from)
{
  web::WebRequest request;
  request.timeout(std::chrono::seconds(15));

  co_return Ok(co_await request.get(
      billing::apiUrl(fmt::format("/chat/stream/{}?from={}", id, from))));
}

billing::ApiResult<api::chat::StreamChunk> api::chat::parseStreamChunk(
    Result<web::WebResponse> const &response)
{
  GEODE_UNWRAP_INTO(auto json, billing::parseJson(response));

  if (auto error = json["error"].asString())
    return Err(billing::ApiError{error.unwrap()});

  StreamChunk chunk;
  chunk.delta = json["delta"].asString().unwrapOr("");
  chunk.next = json["next"].asUInt().unwrapOr(0);

  if (!json["done"].asBool().unwrapOr(false))
    return Ok(std::move(chunk));

  auto reply = json["reply"].asString();
  if (reply.isErr())
    return Err(billing::ApiError{"Invalid server response"});

  ChatMessage message{"assistant", reply.unwrap()};

  // Unknown or broken actions are skipped, navigate.cpp checks the fields
  if (auto actions = json["actions"].asArray())
  {
    for (auto const &item : actions.unwrap())
    {
      auto type = item["type"].asString().unwrapOr("");
      auto label = item["label"].asString().unwrapOr("");
      if (!type.empty() && !label.empty())
        message.actions.push_back({type, label, item});
    }
  }

  chunk.reply = std::move(message);
  return Ok(std::move(chunk));
}
