#include <wx/event.h>
class EventNotifier : public wxEvtHandler {
public:
  static EventNotifier *Get();
};
