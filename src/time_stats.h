#pragma once

#include <cstdint>

namespace swarm {

struct FrameStats {
  double dtSeconds = 0.0;
  double frameMsAvg = 0.0;
  double fpsAvg = 0.0;
};

class TimeStats {
public:
  explicit TimeStats(int window = 120);
  ~TimeStats();
  void push(double dtSeconds);
  FrameStats stats() const;

private:
  int mWindow = 120;
  int mCount = 0;
  int mIndex = 0;
  double mSum = 0.0;
  double* mRing = nullptr;
};

} // namespace swarm

