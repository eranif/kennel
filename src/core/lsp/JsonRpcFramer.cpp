#include "core/lsp/JsonRpcFramer.h"

#include "core/EventNotifier.hpp"

#include <cctype>
#include <cstdlib>

wxDEFINE_EVENT(wxEVT_JSON_RPC_MESSAGE, JsonRpcEvent);

JsonRpcFramer::JsonRpcFramer(LspTransport *transport) : m_transport(transport) {
  EventNotifier::Get()->Bind(wxEVT_LSP_TRANSPORT_STDOUT,
                             &JsonRpcFramer::OnStdout, this);
  m_worker = std::thread(&JsonRpcFramer::Run, this);
}

JsonRpcFramer::~JsonRpcFramer() {
  EventNotifier::Get()->Unbind(wxEVT_LSP_TRANSPORT_STDOUT,
                               &JsonRpcFramer::OnStdout, this);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stop = true;
  }
  m_wake.notify_one();
  m_worker.join();
}

Status JsonRpcFramer::Send(const std::string &body) {
  return m_transport->Send("Content-Length: " + std::to_string(body.size()) +
                           "\r\n\r\n" + body);
}

void JsonRpcFramer::OnStdout(LspTransportEvent &event) {
  if (event.GetEventObject() != m_transport) {
    event.Skip(); // The output of another server
    return;
  }
  event.Skip(false); // Ours: nobody else handles it
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_incoming += event.GetData();
  }
  m_wake.notify_one();
}

void JsonRpcFramer::OnParsed(std::string body) {
  JsonRpcEvent message(wxEVT_JSON_RPC_MESSAGE);
  message.SetEventObject(m_transport);
  message.SetBody(body);
  EventNotifier::Get()->AddPendingEvent(message);
}

void JsonRpcFramer::Run() {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_wake.wait(lock, [this] { return m_stop || !m_incoming.empty(); });
      if (m_stop) {
        return;
      }
      m_buffer += m_incoming;
      m_incoming.clear();
    }
    while (std::optional<std::string> body = ExtractMessage()) {
      CallAfter(&JsonRpcFramer::OnParsed, *body);
    }
  }
}

std::optional<std::string> JsonRpcFramer::ExtractMessage() {
  static constexpr char kHeaderEnd[] = "\r\n\r\n";
  static constexpr char kLengthName[] = "content-length:";
  // A header that does not end is not kept for ever.
  static constexpr size_t kMaxHeaderBytes = 8192;

  for (;;) {
    const size_t headerEnd = m_buffer.find(kHeaderEnd);
    if (headerEnd == std::string::npos) {
      if (m_buffer.size() > kMaxHeaderBytes) {
        m_buffer.clear();
      }
      return std::nullopt;
    }

    // The header lines, up to the empty line: only Content-Length counts.
    long long length = -1;
    size_t lineStart = 0;
    while (lineStart < headerEnd) {
      size_t lineEnd = m_buffer.find("\r\n", lineStart);
      if (lineEnd == std::string::npos || lineEnd > headerEnd) {
        lineEnd = headerEnd;
      }
      bool isLength = lineEnd - lineStart >= sizeof(kLengthName) - 1;
      for (size_t i = 0; isLength && i < sizeof(kLengthName) - 1; ++i) {
        isLength =
            std::tolower(static_cast<unsigned char>(m_buffer[lineStart + i])) ==
            kLengthName[i];
      }
      if (isLength) {
        length =
            std::atoll(m_buffer.c_str() + lineStart + sizeof(kLengthName) - 1);
      }
      lineStart = lineEnd + 2;
    }

    const size_t bodyStart = headerEnd + sizeof(kHeaderEnd) - 1;
    if (length < 0 ||
        static_cast<unsigned long long>(length) > kMaxMessageBytes) {
      // Not the header of a message: skip it and look at what follows.
      m_buffer.erase(0, bodyStart);
      continue;
    }
    if (m_buffer.size() < bodyStart + static_cast<size_t>(length)) {
      return std::nullopt; // The rest has not come yet
    }
    std::string body = m_buffer.substr(bodyStart, static_cast<size_t>(length));
    m_buffer.erase(0, bodyStart + static_cast<size_t>(length));
    return body;
  }
}
