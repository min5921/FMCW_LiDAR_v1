#include "support/point_cloud_replay/async_point_cloud_replay.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace fmcw;
int main() {
  const auto dir=std::filesystem::temp_directory_path()/ ("fmcw_async_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  { std::ofstream large(dir/"large.xyz"); for(int i=0;i<150000;++i) large << "1 2 3\n"; }
  { std::ofstream small(dir/"small.xyz"); small << "9 8 7\n"; }
  AsyncPointCloudReplay reader;
  std::string error;
  reader.open(dir/"large.xyz",error);
  reader.open(dir/"small.xyz",error);
  PointCloudSnapshot frame;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  PointCloudReadResult result;
  do { result=reader.readNext(frame,error); if(result==PointCloudReadResult::Pending) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  while(result==PointCloudReadResult::Pending&&std::chrono::steady_clock::now()<deadline);
  bool valid=result==PointCloudReadResult::FrameReady&&frame.points.size()==1&&frame.points[0].x==9;
  const auto read = [&] {
    const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    PointCloudReadResult next;
    do {
      next=reader.readNext(frame,error);
      if(next==PointCloudReadResult::Pending) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while(next==PointCloudReadResult::Pending&&std::chrono::steady_clock::now()<timeout);
    return next;
  };
  valid = (read()==PointCloudReadResult::EndOfStream) && valid;
  valid = (reader.readNext(frame,error)==PointCloudReadResult::EndOfStream) && valid;
  reader.rewind(error);
  valid = (read()==PointCloudReadResult::FrameReady && frame.points[0].x==9) && valid;
  reader.open(dir/"missing.xyz",error);
  valid = (read()==PointCloudReadResult::Error) && valid;
  valid = (reader.readNext(frame,error)==PointCloudReadResult::Error) && valid;
  reader.open(dir/"small.xyz",error);
  valid = (read()==PointCloudReadResult::FrameReady) && valid;
  reader.close();
  std::error_code cleanup;
  std::filesystem::remove_all(dir,cleanup);
  valid = !cleanup && valid;
  if(!valid) std::cerr << "Old generation leaked or current frame did not load: " << error << '\n';
  return valid?0:1;
}
