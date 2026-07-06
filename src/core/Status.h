#pragma once

#include <wx/string.h>

#include <cstdlib>
#include <new>
#include <utility>

// Lightweight, exception-free result type modeled on absl::Status. An OK
// status carries no message; an error status carries a human-readable message.
class Status {
public:
  Status() = default; // OK

  static Status Ok() { return Status(); }
  static Status Error(const wxString &message) { return Status(message); }

  bool ok() const { return m_ok; }
  const wxString &message() const { return m_message; }

  explicit operator bool() const { return m_ok; }

private:
  explicit Status(const wxString &message) : m_ok(false), m_message(message) {}

  bool m_ok = true;
  wxString m_message;
};

// Holds either a value of type T or an error Status (absl::StatusOr style).
// Never throws: accessing the value of an error result aborts (a programming
// error), so callers MUST check ok() first.
template <typename T> class StatusOr {
public:
  // Implicit construction from a value (the common success path).
  StatusOr(const T &value) : m_ok(true) { new (&m_storage) T(value); }
  StatusOr(T &&value) : m_ok(true) { new (&m_storage) T(std::move(value)); }

  // Implicit construction from an error Status. Constructing from an OK
  // status is a misuse (there is no value) and aborts.
  StatusOr(const Status &status) : m_ok(false), m_status(status) {
    if (status.ok()) {
      std::abort();
    }
  }

  StatusOr(const StatusOr &other) : m_ok(other.m_ok), m_status(other.m_status) {
    if (m_ok) {
      new (&m_storage) T(other.value_ref());
    }
  }
  StatusOr(StatusOr &&other) : m_ok(other.m_ok), m_status(other.m_status) {
    if (m_ok) {
      new (&m_storage) T(std::move(other.value_ref()));
    }
  }

  StatusOr &operator=(const StatusOr &other) {
    if (this != &other) {
      Destroy();
      m_ok = other.m_ok;
      m_status = other.m_status;
      if (m_ok) {
        new (&m_storage) T(other.value_ref());
      }
    }
    return *this;
  }
  StatusOr &operator=(StatusOr &&other) {
    if (this != &other) {
      Destroy();
      m_ok = other.m_ok;
      m_status = other.m_status;
      if (m_ok) {
        new (&m_storage) T(std::move(other.value_ref()));
      }
    }
    return *this;
  }

  ~StatusOr() { Destroy(); }

  bool ok() const { return m_ok; }
  explicit operator bool() const { return m_ok; }

  // The error status (OK if this holds a value).
  Status status() const { return m_ok ? Status::Ok() : m_status; }

  // Accessors. Calling these on an error result is a programming error and
  // aborts (no exceptions). Check ok() first.
  const T &value() const {
    if (!m_ok)
      std::abort();
    return value_ref();
  }
  T &value() {
    if (!m_ok)
      std::abort();
    return value_ref();
  }

  const T &operator*() const { return value(); }
  T &operator*() { return value(); }
  const T *operator->() const { return &value(); }
  T *operator->() { return &value(); }

private:
  const T &value_ref() const {
    return *reinterpret_cast<const T *>(&m_storage);
  }
  T &value_ref() { return *reinterpret_cast<T *>(&m_storage); }

  void Destroy() {
    if (m_ok) {
      value_ref().~T();
    }
  }

  bool m_ok;
  Status m_status;
  alignas(T) unsigned char m_storage[sizeof(T)];
};
