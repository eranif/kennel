#include "core/lsp/JsonRpcFramer.h"

#include "core/lsp/LspClient.h"

#include <cctype>
#include <chrono>
#include <cstdlib>

JsonRpcFramer::JsonRpcFramer(LspClient *client) : m_client(client) {
  m_worker = std::thread(&JsonRpcFramer::Run, this);
}

JsonRpcFramer::~JsonRpcFramer() {
  m_stop = true;
  m_wake.notify_one();
  m_worker.join();
}

void JsonRpcFramer::Feed(std::string buffer) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_incoming += buffer;
  }
  m_wake.notify_one();
}

void JsonRpcFramer::Run() {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      // Nothing came in 10 ms: look again (also at m_stop).
      if (!m_wake.wait_for(lock, std::chrono::milliseconds(10),
                           [this] { return m_stop || !m_incoming.empty(); })) {
        continue;
      }
      if (m_stop) {
        return;
      }
      m_buffer += m_incoming;
      m_incoming.clear();
    }
    while (std::optional<std::string> body = ExtractMessage()) {
      m_client->CallAfter(&LspClient::OnParsed, *body);
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
