#include "core/logging.h"

#include <iomanip>
#include <sstream>

namespace fmcw {

MemoryLog::MemoryLog(std::size_t max_entries) : max_entries_(max_entries) {}

void MemoryLog::write(const LogEntry& entry) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.push_back(entry);
  if (entries_.size() > max_entries_) {
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - max_entries_));
  }
}

void MemoryLog::add(LogLevel level, std::string source, std::string message) {
  write(LogEntry{std::chrono::system_clock::now(), level, std::move(source), std::move(message)});
}

std::vector<LogEntry> MemoryLog::recent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_;
}

void MemoryLog::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
}

std::string toString(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "Debug";
    case LogLevel::Info:
      return "Info";
    case LogLevel::Warning:
      return "Warning";
    case LogLevel::Error:
      return "Error";
    case LogLevel::Critical:
      return "Critical";
  }

  return "Unknown";
}

std::string formatTimestamp(std::chrono::system_clock::time_point timestamp) {
  const auto time = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm_value{};

#if defined(_WIN32)
  localtime_s(&tm_value, &time);
#else
  localtime_r(&time, &tm_value);
#endif

  std::ostringstream text;
  text << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
  return text.str();
}

}  // namespace fmcw
