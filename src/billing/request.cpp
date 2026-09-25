#include "request.hpp"

#include <argon/argon.hpp>

using namespace geode::prelude;

std::string billing::apiUrl(std::string_view path)
{
  auto base = utils::string::trimRight(
      Mod::get()->getSettingValue<std::string>("api-url"), "/");
  return fmt::format("{}{}", base, path);
}

static billing::ResponseFuture sendOnce(
    argon::AccountData const &account, std::string const &version,
    std::string const &method, std::string const &url, billing::RequestFactory const &makeRequest)
{
  auto token = co_await argon::startAuth(account);
  if (token.isErr())
    co_return Err(fmt::format("GD login failed: {}", token.unwrapErr()));

  auto request = makeRequest();
  request.header("X-Argon-Account", std::to_string(account.accountId));
  request.header("X-Argon-Token", token.unwrap());
  request.header("X-AskDash-Version", version);

  co_return Ok(co_await request.send(method, url));
}

static billing::ResponseFuture sendAs(
    argon::AccountData account, std::string version,
    std::string method, std::string url, billing::RequestFactory makeRequest)
{
  if (!account.valid())
    co_return Err("Log in to your GD account to use AskDash");

  auto response = co_await sendOnce(account, version, method, url, makeRequest);

  if (response.isOk() && response.unwrap().code() == 401)
  {
    argon::clearToken(account);
    response = co_await sendOnce(account, version, method, url, makeRequest);
  }

  co_return response;
}

billing::ResponseFuture billing::send(
    std::string method, std::string url, RequestFactory makeRequest)
{
  auto account = argon::signedIn() ? argon::getGameAccountData() : argon::AccountData{};
  return sendAs(
      std::move(account), Mod::get()->getVersion().toVString(),
      std::move(method), std::move(url), std::move(makeRequest));
}
