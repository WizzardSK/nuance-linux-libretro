#pragma once

#include "basetypes.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVIOContext;
struct SwsContext;
#ifdef HAVE_DVDNAV
struct dvdnav_s;
typedef struct dvdnav_s dvdnav_t;
#endif
}

// DVD-Video player for NUON disc menus and FMV cutscenes that ship as
// /VIDEO_TS/*.VOB program streams.
//
// Two modes:
//   File: path to a single VOB / MPEG-PS file. libavformat demuxes it
//         directly and we just decode video frames. No nav, no menus.
//   Disc: path to a DVD root (directory with VIDEO_TS, an .iso, or a
//         block device). libdvdnav drives playback, fires the
//         FP_PGC -> root menu transition, parses NAV packets for
//         button highlights, and accepts user input via the
//         SendUp/Down/Left/Right/Activate methods. The MPEG-PS data
//         is fed into libavformat through a custom AVIOContext, so
//         the existing libavcodec decode path is unchanged.
class DvdPlayer {
public:
  DvdPlayer();
  ~DvdPlayer();

  // Auto-detect: directory or .iso -> Disc mode (libdvdnav).
  // Anything else (.vob, .mpg, .mpeg) -> File mode (libavformat).
  bool Open(const char* path);
  void Close();

  bool IsOpen()  const { return mRunning.load(std::memory_order_acquire); }
  bool IsAtEnd() const { return mEndOfStream.load(std::memory_order_acquire); }
  bool IsDiscMode() const { return mDiscMode; }
  uint32 Width()  const { return mWidth; }
  uint32 Height() const { return mHeight; }

  bool CopyLatestYCrCbA32(uint8* dst, uint32 dstPitchBytes,
                          uint32 dstWidth, uint32 dstHeight);

  // Disc-mode user input (no-op in File mode). Queue a single button
  // command to be consumed by the nav thread on its next iteration.
  enum class ButtonCmd { None, Up, Down, Left, Right, Activate, MenuRoot };
  void SendButton(ButtonCmd cmd);

  // Disc-mode highlight info (raw normalised rect, 0..1 in DVD coords).
  // Returns false if no highlighted button. Useful for overlaying a
  // crude box without parsing SPU subpicture streams.
  bool GetHighlightRectN(float* x, float* y, float* w, float* h) const;
  int  CurrentButton() const { return mCurrentButton.load(std::memory_order_acquire); }
  int  ButtonCount()   const { return mButtonCount.load(std::memory_order_acquire); }

private:
  void DecodeThread();
  void ConvertAndPublish(AVFrame* frame);

  bool OpenFileMode(const char* path);
  bool OpenDiscMode(const char* path);

  // libavformat custom-AVIO read callback (Disc mode).
  static int AvioReadDvd(void* opaque, uint8* buf, int bufSize);
  int ReadDvdBlock(uint8* dst, int dstSize);

  // Apply a queued button command at the next nav callback turn.
  void ApplyPendingButton();

  std::string mPath;
  bool mDiscMode = false;

  AVFormatContext* mFmt = nullptr;
  AVCodecContext*  mAvCtx = nullptr;
  AVFrame*         mFrame = nullptr;
  AVPacket*        mPkt = nullptr;
  AVIOContext*     mAvio = nullptr;
  uint8*           mAvioBuffer = nullptr;
  SwsContext*      mSws = nullptr;
  int              mVideoStreamIndex = -1;

#ifdef HAVE_DVDNAV
  dvdnav_t* mDvdNav = nullptr;
#endif

  uint32 mWidth = 0;
  uint32 mHeight = 0;

  std::mutex mFrameMutex;
  std::vector<uint8> mLatestFrame;
  uint64 mFrameCounter = 0;
  uint64 mLastCopiedFrame = 0;

  // Disc-mode nav state (atomic so video.cpp / input thread can poll
  // freely without taking the frame lock).
  std::atomic<int>  mCurrentButton{0};
  std::atomic<int>  mButtonCount{0};
  std::atomic<int>  mPendingButtonCmd{(int)ButtonCmd::None};
  mutable std::mutex mHighlightMutex;
  float mHlX = 0, mHlY = 0, mHlW = 0, mHlH = 0;
  bool  mHlValid = false;

  std::thread mThread;
  std::atomic<bool> mRunning{false};
  std::atomic<bool> mStopRequested{false};
  std::atomic<bool> mEndOfStream{false};
};
