#pragma once

#include "storage/writer_interfaces.h"

#include <memory>

namespace fmcw {

class AsyncStorageService final : public IStorageService {
 public:
  AsyncStorageService();
  AsyncStorageService(std::unique_ptr<IRawFrameWriter> raw_writer,
                      std::unique_ptr<IPointCloudFrameWriter> point_cloud_writer);
  ~AsyncStorageService() override;

  AsyncStorageService(const AsyncStorageService&) = delete;
  AsyncStorageService& operator=(const AsyncStorageService&) = delete;

  bool start(const WriterOpenOptions& options, std::string& error) override;
  EnqueueResult enqueueRawBatch(RawFrameBatchPtr batch, std::string& error) override;
  EnqueueResult enqueueRaw(RawFramePtr frame, std::string& error) override;
  EnqueueResult enqueuePointCloud(std::shared_ptr<const PointCloudSnapshot> frame,
                                  std::string& error) override;
  void requestStop(std::string reason) override;
  bool waitUntilStopped(std::string& error) override;
  StorageStatus status() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
