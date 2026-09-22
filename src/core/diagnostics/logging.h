#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace fmcw {

enum class LogLevel {
  Debug,
  Info,
  Warning,
  Error,
  Critical,
};

struct LogEntry {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level = LogLevel::Info;
  std::string source;
  std::string message;
};

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const LogEntry& entry) = 0;
};

class MemoryLog final : public LogSink {
 public:
  explicit MemoryLog(std::size_t max_entries = 500);

  void write(const LogEntry& entry) override;
  void add(LogLevel level, std::string source, std::string message);
  std::vector<LogEntry> recent() const;
  void clear();

 private:
  std::size_t max_entries_;
  mutable std::mutex mutex_;
  std::vector<LogEntry> entries_;
};

std::string toString(LogLevel level);
std::string formatTimestamp(std::chrono::system_clock::time_point timestamp);

}  // namespace fmcw
