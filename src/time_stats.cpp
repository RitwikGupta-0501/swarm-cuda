#include "time_stats.h"

#include <algorithm>
#include <new>

namespace swarm {

TimeStats::TimeStats(int window) : mWindow(std::max(1, window)) {
  mRing = new (std::nothrow) double[mWindow];
  if (!mRing) {
    mWindow = 1;
    mRing = new double[1]{0.0};
  }
  for (int i = 0; i < mWindow; ++i) mRing[i] = 0.0;
}

TimeStats::~TimeStats() {
  delete[] mRing;
  mRing = nullptr;
}

void TimeStats::push(double dtSeconds) {
  const double v = std::max(0.0, dtSeconds);
  if (mCount < mWindow) {
    mRing[mIndex] = v;
    mSum += v;
    ++mCount;
  } else {
    mSum -= mRing[mIndex];
    mRing[mIndex] = v;
    mSum += v;
  }
  mIndex = (mIndex + 1) % mWindow;
}

FrameStats TimeStats::stats() const {
  FrameStats s{};
  if (mCount == 0) return s;
  const double avg = mSum / static_cast<double>(mCount);
  s.dtSeconds = (mCount > 0) ? mRing[(mIndex + mWindow - 1) % mWindow] : 0.0;
  s.frameMsAvg = avg * 1000.0;
  s.fpsAvg = (avg > 0.0) ? (1.0 / avg) : 0.0;
  return s;
}

} // namespace swarm

