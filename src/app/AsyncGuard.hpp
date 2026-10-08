#pragma once

#include <wx/app.h>

#include <atomic>
#include <memory>
#include <utility>

// Worker threads (SFTP checks) end by handing their result to the UI thread
// with CallAfter(). The object that wanted the result may be gone by then. The
// owner keeps an AliveFlag, sets it to false in its destructor, and the worker
// goes through CallAfterIfAlive():
//
//   m_alive = MakeAliveFlag();               // member
//   ~Owner() { m_alive->store(false); }
//   std::thread([this, alive = m_alive] {
//     ...                                    // blocking work, no member access
//     CallAfterIfAlive(alive, [this] { ... }); // runs on the UI thread
//   }).detach();
//
// The flag is checked twice: before queueing (the app may be shutting down) and
// again on the UI thread (the owner may have died while the call was queued).
// A worker must not touch `this` outside the callback.
using AliveFlag = std::shared_ptr<std::atomic<bool>>;

inline AliveFlag MakeAliveFlag() {
  return std::make_shared<std::atomic<bool>>(true);
}

template <typename Fn> void CallAfterIfAlive(const AliveFlag &alive, Fn fn) {
  // wxTheApp is gone once the application has shut down.
  if (!alive->load() || wxTheApp == nullptr) {
    return;
  }
  wxTheApp->CallAfter([alive, fn = std::move(fn)]() mutable {
    if (alive->load()) {
      fn();
    }
  });
}
