#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Text WebSocket client, Windows only (WinHTTP). Handlers run on the socket's own thread
namespace live
{
  struct SocketHandlers
  {
    std::function<void()> onOpen;
    std::function<void(std::string)> onMessage;
    // The server answered the upgrade with an HTTP error, body is its JSON
    std::function<void(int status, std::string body)> onRejected;
    // Connection lost or closed, also after a failed connect
    std::function<void()> onClosed;
  };

  class Socket
  {
  public:
    virtual ~Socket() = default;

    // Thread safe, dropped when not connected
    virtual void send(std::string const &text) = 0;
    // Doesn't wait, onClosed comes later from the socket thread
    virtual void close() = 0;

    // nullptr where WebSockets aren't supported
    static std::shared_ptr<Socket> connect(
        std::string url, std::vector<std::pair<std::string, std::string>> headers, SocketHandlers handlers);
  };
}
