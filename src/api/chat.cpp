#include "chat.hpp"
#include "context.hpp"

#include <chrono>

using namespace geode::prelude;

static constexpr size_t MAX_HISTORY = 20;

static std::string getApiUrl()
{
  return utils::string::trimRight(
      Mod::get()->getSettingValue<std::string>("api-url"), "/");
}

web::WebFuture api::chat::sendMessages(
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

  web::WebRequest request;

  request.header("Content-Type", "application/json");
  auto body = matjson::makeObject({{"messages", jsonMessages}});
  if (Mod::get()->getSettingValue<bool>("send-game-context"))
    body["context"] = api::context::collect();

  request.bodyJSON(body);
  request.timeout(std::chrono::seconds(60));

  return request.post(fmt::format("{}/chat", getApiUrl()));
}

geode::Result<std::string> api::chat::parseReply(
    web::WebResponse const &response)
{
  auto jsonResult = response.json();

  if (!response.ok())
  {
    if (response.code() <= 0)
      return geode::Err("Can't reach the server, check the API URL in mod settings");

    if (response.code() == 429)
      return geode::Err("Too many requests, try again later");

    if (jsonResult.isOk())
    {
      auto error = jsonResult.unwrap()["error"].asString();
      if (error.isOk())
        return geode::Err(error.unwrap());
    }

    return geode::Err(fmt::format("Server error (HTTP {})", response.code()));
  }

  if (jsonResult.isErr())
    return geode::Err("Invalid server response");

  auto reply = jsonResult.unwrap()["reply"].asString();

  if (reply.isErr())
    return geode::Err("Invalid server response");

  return geode::Ok(reply.unwrap());
}
