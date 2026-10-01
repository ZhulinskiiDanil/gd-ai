#include "socket.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

#include <atomic>
#include <mutex>
#include <thread>
#include <windows.h>
#include <winhttp.h>

using namespace geode::prelude;

static std::wstring widen(std::string_view text)
{
  if (text.empty())
    return {};

  int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

class WinSocket final : public live::Socket, public std::enable_shared_from_this<WinSocket>
{
  std::string m_url;
  std::vector<std::pair<std::string, std::string>> m_headers;
  live::SocketHandlers m_handlers;

  // Guards the handles: close() may come from any thread while the socket thread blocks on them
  std::mutex m_mutex;
  HINTERNET m_session = nullptr;
  HINTERNET m_connection = nullptr;
  HINTERNET m_request = nullptr;
  HINTERNET m_socket = nullptr;
  std::atomic<bool> m_closing = false;
  std::mutex m_sendMutex;

public:
  WinSocket(std::string url, std::vector<std::pair<std::string, std::string>> headers, live::SocketHandlers handlers)
      : m_url(std::move(url)), m_headers(std::move(headers)), m_handlers(std::move(handlers)) {}

  ~WinSocket() override
  {
    closeHandles();
  }

  void start()
  {
    std::thread([self = shared_from_this()]
                {
      self->run();
      self->closeHandles();
      if (self->m_handlers.onClosed)
        self->m_handlers.onClosed(); })
        .detach();
  }

  void send(std::string const &text) override
  {
    std::lock_guard sendLock(m_sendMutex);
    HINTERNET socket;
    {
      std::lock_guard lock(m_mutex);
      socket = m_socket;
    }
    if (!socket || m_closing)
      return;

    WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                         const_cast<char *>(text.data()), static_cast<DWORD>(text.size()));
  }

  void close() override
  {
    m_closing = true;

    // Closing the handle the socket thread blocks on makes its call fail and the thread end
    std::lock_guard lock(m_mutex);
    if (m_socket)
    {
      WinHttpCloseHandle(m_socket);
      m_socket = nullptr;
    }
    if (m_request)
    {
      WinHttpCloseHandle(m_request);
      m_request = nullptr;
    }
  }

private:
  void closeHandles()
  {
    std::lock_guard lock(m_mutex);
    for (auto handle : {&m_socket, &m_request, &m_connection, &m_session})
    {
      if (*handle)
      {
        WinHttpCloseHandle(*handle);
        *handle = nullptr;
      }
    }
  }

  // Body of an HTTP error answer to the upgrade
  std::string readBody(HINTERNET request)
  {
    std::string body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0 && body.size() < 16 * 1024)
    {
      std::string chunk(available, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
        break;
      body.append(chunk.data(), read);
    }
    return body;
  }

  void run()
  {
    auto url = widen(m_url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
    {
      log::error("Live talk: bad URL {}", m_url);
      return;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET request;
    {
      std::lock_guard lock(m_mutex);
      if (m_closing)
        return;

      m_session = WinHttpOpen(L"AskDash", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      m_connection = m_session ? WinHttpConnect(m_session, host.c_str(), parts.nPort, 0) : nullptr;
      m_request = m_connection ? WinHttpOpenRequest(m_connection, L"GET", path.c_str(), nullptr,
                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    secure ? WINHTTP_FLAG_SECURE : 0)
                               : nullptr;
      request = m_request;
    }

    if (!request)
    {
      log::error("Live talk: WinHTTP setup failed ({})", GetLastError());
      return;
    }

    std::wstring headers;
    for (auto const &[name, value] : m_headers)
      headers += widen(name) + L": " + widen(value) + L"\r\n";

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    bool sent = WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
                WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD) &&
                WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr) &&
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if (!sent)
    {
      if (!m_closing)
        log::error("Live talk: connecting failed ({})", GetLastError());
      return;
    }

    if (status != 101)
    {
      auto body = readBody(request);
      if (m_handlers.onRejected && !m_closing)
        m_handlers.onRejected(static_cast<int>(status), std::move(body));
      return;
    }

    HINTERNET socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    {
      std::lock_guard lock(m_mutex);
      WinHttpCloseHandle(m_request);
      m_request = nullptr;
      if (!socket || m_closing)
      {
        if (socket)
          WinHttpCloseHandle(socket);
        return;
      }
      m_socket = socket;
    }

    if (m_handlers.onOpen)
      m_handlers.onOpen();

    // ! --- Receive loop --- !
    std::vector<char> buffer(64 * 1024);
    std::string message;

    while (!m_closing)
    {
      DWORD read = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
      if (WinHttpWebSocketReceive(socket, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type) != NO_ERROR)
        break;

      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        break;

      message.append(buffer.data(), read);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
      {
        if (m_handlers.onMessage)
          m_handlers.onMessage(std::move(message));
        message.clear();
      }
      else if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
        message.clear();
    }
  }
};

std::shared_ptr<live::Socket> live::Socket::connect(
    std::string url, std::vector<std::pair<std::string, std::string>> headers, SocketHandlers handlers)
{
  auto socket = std::make_shared<WinSocket>(std::move(url), std::move(headers), std::move(handlers));
  socket->start();
  return socket;
}

#else

std::shared_ptr<live::Socket> live::Socket::connect(
    std::string, std::vector<std::pair<std::string, std::string>>, SocketHandlers)
{
  return nullptr;
}

#endif
