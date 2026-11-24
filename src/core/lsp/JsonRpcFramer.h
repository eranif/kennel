#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class LspClient;

// The framing of JSON-RPC in the Language Server Protocol: every message is
// `Content-Length: <n>\r\n\r\n` and then n bytes of JSON (UTF-8).
//
// It works for one LspClient. Feed() gives it what the server wrote; a thread
// of its own cuts that into messages (a long message must not stall the UI) and
// calls LspClient::OnParsed() for each one, on the UI thread (CallAfter()).
// GUI-free.
class JsonRpcFramer {
public:
  // A message that claims to be longer is not a message: its header is skipped.
  static constexpr size_t kMaxMessageBytes = 256 * 1024 * 1024;

  // `client` must outlive the framer.
  explicit JsonRpcFramer(LspClient *client);
  // Stops the thread and waits for it.
  ~JsonRpcFramer();

  JsonRpcFramer(const JsonRpcFramer &) = delete;
  JsonRpcFramer &operator=(const JsonRpcFramer &) = delete;

  // `buffer` is what the server wrote: it may end in the middle of a message,
  // and may hold several. Any thread may call it.
  void Feed(std::string buffer);

private:
  // The thread: waits for bytes, and gives each whole message to the client,
  // with CallAfter(). It is started with `this`, which is valid for as long as
  // it runs: the destructor joins it.
  void Run();
  // Takes the next whole message out of `m_buffer`, or nothing when there is
  // none yet. Text that is not a message (what a wrapper program prints before
  // the server starts) is skipped, so that the next message is still found.
  // Only the thread calls it.
  std::optional<std::string> ExtractMessage();

  LspClient *m_client;

  std::mutex m_mutex;
  std::condition_variable m_wake;
  // Bytes that came and the thread did not take yet. Guarded by `m_mutex`.
  std::string m_incoming;
  // The thread must end.
  std::atomic_bool m_stop{false};
  // What the thread took and is not a whole message yet. Only the thread uses
  // it.
  std::string m_buffer;
  std::thread m_worker;
};
