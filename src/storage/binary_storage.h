#pragma once

#include "storage/writer_interfaces.h"

#include <filesystem>
#include <memory>

namespace fmcw {

class BinaryRawFrameWriter final : public IRawFrameWriter {
 public:
  BinaryRawFrameWriter();
  ~BinaryRawFrameWriter() override;

  bool open(const WriterOpenOptions& options, std::string& error) override;
  bool write(const RawFrame& frame, std::string& error) override;
  bool flush(std::string& error) override;
  bool finalize(const WriterFinalizeOptions& options, std::string& error) override;
  WriterStatus status() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class BinaryProcessedFrameWriter final : public IProcessedFrameWriter {
 public:
  BinaryProcessedFrameWriter();
  ~BinaryProcessedFrameWriter() override;

  bool open(const WriterOpenOptions& options, std::string& error) override;
  bool write(const ProcessedFrame& frame, std::string& error) override;
  bool flush(std::string& error) override;
  bool finalize(const WriterFinalizeOptions& options, std::string& error) override;
  WriterStatus status() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

enum class ReplayReadResult {
  FrameReady,
  EndOfStream,
  Error,
};

class RawReplayReader {
 public:
  RawReplayReader();
  ~RawReplayReader();

  bool open(const std::filesystem::path& path, std::string& error);
  ReplayReadResult readNext(RawFrame& frame, std::string& error);
  void close();
  bool isOpen() const;
  const RawStreamDescriptor& streamDescriptor() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
