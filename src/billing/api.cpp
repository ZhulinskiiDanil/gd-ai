#include "api.hpp"

#include <chrono>

using namespace geode::prelude;

static constexpr auto TIMEOUT = std::chrono::seconds(15);

billing::ApiResult<matjson::Value> billing::parseJson(Result<web::WebResponse> const &result)
{
  if (result.isErr())
    return Err(ApiError{result.unwrapErr()});

  auto const &response = result.unwrap();
  auto json = response.json();

  if (!response.ok())
  {
    if (response.code() <= 0)
      return Err(ApiError{"Can't reach the server, check the API URL in mod settings"});

    if (response.code() == 429)
      return Err(ApiError{"Too many requests, try again later"});

    if (json.isOk())
    {
      if (auto error = json.unwrap()["error"].asString())
        return Err(ApiError{error.unwrap(), json.unwrap()["upgrade"].asBool().unwrapOr(false)});
    }

    return Err(ApiError{fmt::format("Server error (HTTP {})", response.code())});
  }

  if (json.isErr())
    return Err(ApiError{"Invalid server response"});

  return Ok(json.unwrap());
}

static web::WebRequest makeRequest()
{
  web::WebRequest request;
  request.timeout(TIMEOUT);
  return request;
}

billing::ResponseFuture billing::fetchStatus()
{
  return send("GET", apiUrl("/me"), makeRequest);
}

billing::ApiResult<billing::Status> billing::parseStatus(Result<web::WebResponse> const &response)
{
  GEODE_UNWRAP_INTO(auto json, parseJson(response));

  return Ok(Status{
      json["plan"].asString().unwrapOr("free"),
      json["planName"].asString().unwrapOr("Free"),
      json["unlimited"].asBool().unwrapOr(false),
      static_cast<int>(json["spentPercent"].asInt().unwrapOr(0)),
  });
}

billing::ResponseFuture billing::fetchPlans()
{
  return send("GET", apiUrl("/plans"), makeRequest);
}

billing::ApiResult<billing::Plans> billing::parsePlans(Result<web::WebResponse> const &response)
{
  GEODE_UNWRAP_INTO(auto json, parseJson(response));

  Plans plans;
  plans.buyUrl = json["buyUrl"].asString().unwrapOr("");

  for (auto const &plan : json["plans"])
    plans.plans.push_back({
        plan["name"].asString().unwrapOr(""),
        plan["price"].asString().unwrapOr(""),
    });

  return Ok(std::move(plans));
}

billing::ResponseFuture billing::activateKey(std::string key)
{
  return send("POST", apiUrl("/license"), [key = std::move(key)]
              {
    auto request = makeRequest();
    request.bodyJSON(matjson::makeObject({{"key", key}}));
    return request; });
}

std::string billing::describe(Status const &status)
{
  if (status.unlimited)
    return fmt::format("{}: no limit", status.planName);

  // Paid limits refill every 30 days from activation
  auto left = 100 - status.spentPercent;

  return status.plan == "free"
             ? fmt::format("{}: {}% left today", status.planName, left)
             : fmt::format("{}: {}% left", status.planName, left);
}
