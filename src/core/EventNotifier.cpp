#include "EventNotifier.hpp"

EventNotifier *EventNotifier::Get() {
  static EventNotifier notifier;
  return &notifier;
}
