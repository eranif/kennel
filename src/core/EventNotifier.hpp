#pragma once

#include <wx/event.h>

// Process-wide event hub. Anyone can send an event through it (usually with
// AddPendingEvent()) and anyone can Bind() to it, so the sender and the
// listeners do not need to know each other. A listener must Unbind() before
// it is destroyed: the notifier outlives every window.
class EventNotifier : public wxEvtHandler {
public:
  static EventNotifier *Get();
};
