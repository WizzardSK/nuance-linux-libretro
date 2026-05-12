#include "dvd_player.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#ifdef HAVE_DVDNAV
extern "C" {
#include <dvdnav/dvdnav.h>
#include <dvdread/nav_types.h>
#include <dvdread/nav_read.h>
}
#endif

namespace {
inline uint32 BgraToYCrCbA(uint8 b, uint8 g, uint8 r) {
  const int y  = ( 66 * r + 129 * g +  25 * b + 128) >> 8;
  const int cb = (-38 * r -  74 * g + 112 * b + 128) >> 8;
  const int cr = (112 * r -  94 * g -  18 * b + 128) >> 8;
  const uint8 Y  = (uint8)(y  + 16);
  const uint8 Cb = (uint8)(cb + 128);
  const uint8 Cr = (uint8)(cr + 128);
  return ((uint32)0xFF << 24) | ((uint32)Cr << 16) | ((uint32)Cb << 8) | Y;
}

bool LooksLikeDvdRoot(const char* path) {
  if (!path || !*path) return false;
  struct stat st{};
  if (stat(path, &st) != 0) return false;
  if (S_ISREG(st.st_mode)) {
    // Could be an .iso. Check by extension.
    const char* dot = strrchr(path, '.');
    if (dot && (strcasecmp(dot, ".iso") == 0)) return true;
    return false;
  }
  if (!S_ISDIR(st.st_mode)) return false;
  // Directory: look for VIDEO_TS / video_ts.
  std::string candidates[] = {
    std::string(path) + "/VIDEO_TS",
    std::string(path) + "/video_ts",
    std::string(path) + "/VIDEO_TS/VIDEO_TS.IFO",
    std::string(path) + "/video_ts/video_ts.ifo",
  };
  for (const auto& c : candidates) {
    struct stat s{};
    if (stat(c.c_str(), &s) == 0) return true;
  }
  return false;
}
} // namespace

DvdPlayer::DvdPlayer() = default;

DvdPlayer::~DvdPlayer() {
  Close();
}

bool DvdPlayer::Open(const char* path) {
  Close();
  mPath = path ? path : "";

#ifdef HAVE_DVDNAV
  if (LooksLikeDvdRoot(mPath.c_str())) {
    return OpenDiscMode(mPath.c_str());
  }
#endif
  return OpenFileMode(mPath.c_str());
}

bool DvdPlayer::OpenFileMode(const char* path) {
  mDiscMode = false;
  if (avformat_open_input(&mFmt, path, nullptr, nullptr) < 0) {
    fprintf(stderr, "[DVD] avformat_open_input failed: %s\n", path);
    return false;
  }
  if (avformat_find_stream_info(mFmt, nullptr) < 0) {
    fprintf(stderr, "[DVD] avformat_find_stream_info failed: %s\n", path);
    avformat_close_input(&mFmt);
    return false;
  }

  mVideoStreamIndex = -1;
  for (unsigned i = 0; i < mFmt->nb_streams; i++) {
    if (mFmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      mVideoStreamIndex = (int)i;
      break;
    }
  }
  if (mVideoStreamIndex < 0) {
    fprintf(stderr, "[DVD] no video stream in %s\n", path);
    avformat_close_input(&mFmt);
    return false;
  }

  AVCodecParameters* par = mFmt->streams[mVideoStreamIndex]->codecpar;
  const AVCodec* codec = avcodec_find_decoder(par->codec_id);
  if (!codec) {
    fprintf(stderr, "[DVD] avcodec_find_decoder(%d) failed\n", (int)par->codec_id);
    avformat_close_input(&mFmt);
    return false;
  }

  mAvCtx = avcodec_alloc_context3(codec);
  if (!mAvCtx) { avformat_close_input(&mFmt); return false; }
  if (avcodec_parameters_to_context(mAvCtx, par) < 0) {
    avcodec_free_context(&mAvCtx);
    avformat_close_input(&mFmt);
    return false;
  }
  if (avcodec_open2(mAvCtx, codec, nullptr) < 0) {
    avcodec_free_context(&mAvCtx);
    avformat_close_input(&mFmt);
    return false;
  }

  mPkt   = av_packet_alloc();
  mFrame = av_frame_alloc();
  if (!mPkt || !mFrame) { Close(); return false; }

  fprintf(stderr, "[DVD] file mode: %s (%dx%d codec=%s)\n",
          path, par->width, par->height, codec->name);

  mEndOfStream.store(false, std::memory_order_release);
  mStopRequested.store(false, std::memory_order_release);
  mRunning.store(true, std::memory_order_release);
  mThread = std::thread(&DvdPlayer::DecodeThread, this);
  return true;
}

#ifdef HAVE_DVDNAV
bool DvdPlayer::OpenDiscMode(const char* path) {
  mDiscMode = true;
  if (dvdnav_open(&mDvdNav, path) != DVDNAV_STATUS_OK) {
    fprintf(stderr, "[DVD] dvdnav_open failed: %s\n", path);
    return false;
  }

  // Read-ahead caching gives smoother playback at the cost of a small
  // delay before nav events become visible. Disable it so PCI/HIGHLIGHT
  // events arrive promptly.
  dvdnav_set_readahead_flag(mDvdNav, 0);
  dvdnav_set_PGC_positioning_flag(mDvdNav, 1);

  // Force English titles where available; harmless if disc has no
  // language tags.
  dvdnav_menu_language_select(mDvdNav, (char*)"en");
  dvdnav_audio_language_select(mDvdNav, (char*)"en");
  dvdnav_spu_language_select(mDvdNav, (char*)"en");

  // Custom AVIO context so libavformat reads MPEG-PS bytes directly
  // from libdvdnav. 32 KiB internal buffer matches what the MPEG-PS
  // demuxer prefers.
  const int kAvioBufSize = 32 * 1024;
  mAvioBuffer = (uint8*)av_malloc(kAvioBufSize);
  if (!mAvioBuffer) { Close(); return false; }
  mAvio = avio_alloc_context(mAvioBuffer, kAvioBufSize,
                             /*write_flag=*/0,
                             /*opaque=*/this,
                             &DvdPlayer::AvioReadDvd,
                             /*write_packet=*/nullptr,
                             /*seek=*/nullptr);
  if (!mAvio) { Close(); return false; }
  mAvio->seekable = 0;

  mFmt = avformat_alloc_context();
  if (!mFmt) { Close(); return false; }
  mFmt->pb = mAvio;
  mFmt->flags |= AVFMT_FLAG_CUSTOM_IO;

  // Hint the MPEG-PS demuxer; otherwise the probe might fail for some
  // streams without a clean leading pack header.
  const AVInputFormat* inFmt = av_find_input_format("mpeg");

  if (avformat_open_input(&mFmt, nullptr, (AVInputFormat*)inFmt, nullptr) < 0) {
    fprintf(stderr, "[DVD] avformat_open_input(custom AVIO) failed\n");
    Close();
    return false;
  }
  if (avformat_find_stream_info(mFmt, nullptr) < 0) {
    fprintf(stderr, "[DVD] avformat_find_stream_info(custom AVIO) failed\n");
    Close();
    return false;
  }

  mVideoStreamIndex = -1;
  for (unsigned i = 0; i < mFmt->nb_streams; i++) {
    if (mFmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      mVideoStreamIndex = (int)i;
      break;
    }
  }
  if (mVideoStreamIndex < 0) {
    fprintf(stderr, "[DVD] disc mode: no video stream\n");
    Close();
    return false;
  }

  AVCodecParameters* par = mFmt->streams[mVideoStreamIndex]->codecpar;
  const AVCodec* codec = avcodec_find_decoder(par->codec_id);
  if (!codec) { Close(); return false; }
  mAvCtx = avcodec_alloc_context3(codec);
  if (!mAvCtx) { Close(); return false; }
  if (avcodec_parameters_to_context(mAvCtx, par) < 0) { Close(); return false; }
  if (avcodec_open2(mAvCtx, codec, nullptr) < 0) { Close(); return false; }

  mPkt   = av_packet_alloc();
  mFrame = av_frame_alloc();
  if (!mPkt || !mFrame) { Close(); return false; }

  const char* title = nullptr;
  dvdnav_get_title_string(mDvdNav, &title);
  fprintf(stderr, "[DVD] disc mode opened: %s [title=%s] (%dx%d codec=%s)\n",
          path, title ? title : "?", par->width, par->height, codec->name);

  mEndOfStream.store(false, std::memory_order_release);
  mStopRequested.store(false, std::memory_order_release);
  mRunning.store(true, std::memory_order_release);
  mThread = std::thread(&DvdPlayer::DecodeThread, this);
  return true;
}
#else
bool DvdPlayer::OpenDiscMode(const char* path) {
  fprintf(stderr, "[DVD] disc mode requested but built without libdvdnav: %s\n", path);
  return false;
}
#endif

void DvdPlayer::Close() {
  mStopRequested.store(true, std::memory_order_release);
  if (mThread.joinable()) mThread.join();
  mRunning.store(false, std::memory_order_release);

  if (mPkt)   { av_packet_free(&mPkt);   mPkt   = nullptr; }
  if (mFrame) { av_frame_free(&mFrame);  mFrame = nullptr; }
  if (mAvCtx) { avcodec_free_context(&mAvCtx); mAvCtx = nullptr; }
  if (mSws)   { sws_freeContext(mSws);   mSws   = nullptr; }
  if (mFmt)   { avformat_close_input(&mFmt); mFmt = nullptr; }
  if (mAvio)  {
    // avformat_close_input frees pb if it owned it; if not (custom IO),
    // avio_context_free below releases the wrapper but not the buffer.
    av_freep(&mAvio->buffer);
    avio_context_free(&mAvio);
    mAvio = nullptr;
    mAvioBuffer = nullptr;
  }
#ifdef HAVE_DVDNAV
  if (mDvdNav) { dvdnav_close(mDvdNav); mDvdNav = nullptr; }
#endif

  mWidth = mHeight = 0;
  mFrameCounter = 0;
  mLastCopiedFrame = 0;
  mEndOfStream.store(false, std::memory_order_release);
  // Clear stop flag so the next Open() can drive AVIO callbacks.
  mStopRequested.store(false, std::memory_order_release);
  mDiscMode = false;
  mCurrentButton.store(0, std::memory_order_release);
  mButtonCount.store(0, std::memory_order_release);
  mPendingButtonCmd.store((int)ButtonCmd::None, std::memory_order_release);
}

void DvdPlayer::DecodeThread() {
  uint64 framesDecoded = 0;
  while (!mStopRequested.load(std::memory_order_acquire)) {
    int rd = av_read_frame(mFmt, mPkt);
    if (rd < 0) {
      avcodec_send_packet(mAvCtx, nullptr);
      while (avcodec_receive_frame(mAvCtx, mFrame) == 0) {
        ConvertAndPublish(mFrame);
        framesDecoded++;
      }
      fprintf(stderr, "[DVD] EOF (%llu frames decoded)\n",
              (unsigned long long)framesDecoded);
      mEndOfStream.store(true, std::memory_order_release);
      break;
    }
    if (mPkt->stream_index != mVideoStreamIndex) {
      av_packet_unref(mPkt);
      continue;
    }
    if (avcodec_send_packet(mAvCtx, mPkt) >= 0) {
      while (avcodec_receive_frame(mAvCtx, mFrame) == 0) {
        ConvertAndPublish(mFrame);
        framesDecoded++;
      }
    }
    av_packet_unref(mPkt);
    if (!mDiscMode) {
      // Pace the file path slightly so we don't burn CPU on a fast
      // file. The disc path is naturally paced by libdvdnav.
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
}

void DvdPlayer::ConvertAndPublish(AVFrame* frame) {
  if (frame->width <= 0 || frame->height <= 0) return;
  const uint32 W = (uint32)frame->width;
  const uint32 H = (uint32)frame->height;

  if (!mSws || mWidth != W || mHeight != H) {
    if (mSws) { sws_freeContext(mSws); mSws = nullptr; }
    mSws = sws_getContext(W, H, (AVPixelFormat)frame->format,
                          W, H, AV_PIX_FMT_BGRA,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!mSws) return;
    mWidth = W;
    mHeight = H;
  }

  std::vector<uint8> bgra(W * H * 4);
  uint8* dst[4] = { bgra.data(), nullptr, nullptr, nullptr };
  int dstStride[4] = { (int)(W * 4), 0, 0, 0 };
  sws_scale(mSws, frame->data, frame->linesize, 0, (int)H, dst, dstStride);

  std::vector<uint8> packed(W * H * 4);
  for (uint32 y = 0; y < H; y++) {
    const uint8* srcRow = bgra.data() + y * W * 4;
    uint32* dstRow = (uint32*)(packed.data() + y * W * 4);
    for (uint32 x = 0; x < W; x++) {
      const uint8 b = srcRow[x*4 + 0];
      const uint8 g = srcRow[x*4 + 1];
      const uint8 r = srcRow[x*4 + 2];
      dstRow[x] = BgraToYCrCbA(b, g, r);
    }
  }

  std::lock_guard<std::mutex> lk(mFrameMutex);
  mLatestFrame = std::move(packed);
  mFrameCounter++;
}

bool DvdPlayer::CopyLatestYCrCbA32(uint8* dst, uint32 dstPitchBytes,
                                   uint32 dstWidth, uint32 dstHeight) {
  std::lock_guard<std::mutex> lk(mFrameMutex);
  if (mLatestFrame.empty() || mWidth == 0 || mHeight == 0) return false;
  if (mFrameCounter == mLastCopiedFrame) return false;
  if (dstWidth != mWidth || dstHeight != mHeight) return false;
  for (uint32 y = 0; y < mHeight; y++) {
    memcpy(dst + y * dstPitchBytes,
           mLatestFrame.data() + y * mWidth * 4,
           mWidth * 4);
  }
  mLastCopiedFrame = mFrameCounter;
  return true;
}

void DvdPlayer::SendButton(ButtonCmd cmd) {
  if (!mDiscMode) return;
  mPendingButtonCmd.store((int)cmd, std::memory_order_release);
}

bool DvdPlayer::GetHighlightRectN(float* x, float* y, float* w, float* h) const {
  std::lock_guard<std::mutex> lk(mHighlightMutex);
  if (!mHlValid) return false;
  if (x) *x = mHlX;
  if (y) *y = mHlY;
  if (w) *w = mHlW;
  if (h) *h = mHlH;
  return true;
}

#ifdef HAVE_DVDNAV
void DvdPlayer::ApplyPendingButton() {
  const int v = mPendingButtonCmd.exchange((int)ButtonCmd::None, std::memory_order_acq_rel);
  const ButtonCmd cmd = (ButtonCmd)v;
  if (cmd == ButtonCmd::None) return;

  if (cmd == ButtonCmd::MenuRoot) {
    dvdnav_menu_call(mDvdNav, DVD_MENU_Root);
    fprintf(stderr, "[DVD] menu_call(Root)\n");
    return;
  }

  pci_t* pci = dvdnav_get_current_nav_pci(mDvdNav);
  if (!pci) return;

  switch (cmd) {
    case ButtonCmd::Up:       dvdnav_upper_button_select(mDvdNav, pci);  break;
    case ButtonCmd::Down:     dvdnav_lower_button_select(mDvdNav, pci);  break;
    case ButtonCmd::Left:     dvdnav_left_button_select(mDvdNav,  pci);  break;
    case ButtonCmd::Right:    dvdnav_right_button_select(mDvdNav, pci);  break;
    case ButtonCmd::Activate: dvdnav_button_activate(mDvdNav, pci);      break;
    default: break;
  }

  int32_t btn = 0;
  dvdnav_get_current_highlight(mDvdNav, &btn);
  mCurrentButton.store(btn, std::memory_order_release);
  fprintf(stderr, "[DVD] btn cmd %d -> highlight=%d\n", v, btn);
}

int DvdPlayer::AvioReadDvd(void* opaque, uint8* buf, int bufSize) {
  return ((DvdPlayer*)opaque)->ReadDvdBlock(buf, bufSize);
}

int DvdPlayer::ReadDvdBlock(uint8* dst, int dstSize) {
  static int s_evtTrace = -1;
  if (s_evtTrace < 0) s_evtTrace = getenv("NUANCE_DVD_TRACE") ? 1 : 0;
  if (dstSize < 2048) return AVERROR(EINVAL);

  uint8 navBuf[2048];
  static const char* kEvtName[] = {
    "BLOCK_OK", "NOP", "STILL_FRAME", "SPU_STREAM_CHANGE",
    "AUDIO_STREAM_CHANGE", "VTS_CHANGE", "CELL_CHANGE", "NAV_PACKET",
    "STOP", "HIGHLIGHT", "SPU_CLUT_CHANGE", "HOP_CHANNEL", "WAIT"
  };

  while (!mStopRequested.load(std::memory_order_acquire)) {
    ApplyPendingButton();

    int32_t evt = 0;
    int32_t len = 0;
    if (dvdnav_get_next_block(mDvdNav, navBuf, &evt, &len) == DVDNAV_STATUS_ERR) {
      fprintf(stderr, "[DVD] dvdnav_get_next_block err: %s\n",
              dvdnav_err_to_string(mDvdNav));
      return 0; // EOF
    }
    if (s_evtTrace) {
      const char* en = (evt >= 0 && evt < (int)(sizeof(kEvtName)/sizeof(*kEvtName)))
                         ? kEvtName[evt] : "?";
      fprintf(stderr, "[DVD-EVT] %s len=%d\n", en, len);
      fflush(stderr);
    }

    switch (evt) {
      case DVDNAV_BLOCK_OK:
        if (len > dstSize) return AVERROR(EINVAL);
        memcpy(dst, navBuf, len);
        return len;

      case DVDNAV_NAV_PACKET: {
        // Already-delivered block was a NAV packet. Update highlight
        // info from the now-current PCI. We still pass the bytes
        // through to the demuxer so its PTS bookkeeping is consistent.
        pci_t* pci = dvdnav_get_current_nav_pci(mDvdNav);
        if (pci) {
          int32_t hl = 0;
          dvdnav_get_current_highlight(mDvdNav, &hl);
          mCurrentButton.store(hl, std::memory_order_release);
          mButtonCount.store(pci->hli.hl_gi.btn_ns, std::memory_order_release);
          if (hl > 0 && hl <= pci->hli.hl_gi.btn_ns) {
            const btni_t& b = pci->hli.btnit[hl - 1];
            std::lock_guard<std::mutex> lk(mHighlightMutex);
            // pci coordinates are in pixels, scale to 0..1 against
            // the menu video resolution (typically 720x480).
            const float vw = 720.0f, vh = 480.0f;
            mHlX = (float)b.x_start / vw;
            mHlY = (float)b.y_start / vh;
            mHlW = (float)(b.x_end - b.x_start) / vw;
            mHlH = (float)(b.y_end - b.y_start) / vh;
            mHlValid = true;
          }
        }
        // NAV packets are also valid MPEG-PS pack data — pass through.
        if (len > dstSize) return AVERROR(EINVAL);
        memcpy(dst, navBuf, len);
        return len;
      }

      case DVDNAV_STILL_FRAME: {
        // Hold the current frame. If the still has finite length,
        // dvdnav handles the timer once we ack with still_skip.
        // Infinite stills (length 0xff) wait for user input — for
        // now we ack instantly only if a button-activate was queued.
        const dvdnav_still_event_t* still = (const dvdnav_still_event_t*)navBuf;
        if (still->length == 0xff) {
          // Pump pending input once; if Activate was pressed we let
          // it apply on the next iteration which usually moves us
          // off the still automatically. Sleep briefly to avoid spin.
          if (mPendingButtonCmd.load() == (int)ButtonCmd::Activate) {
            ApplyPendingButton();
            dvdnav_still_skip(mDvdNav);
          } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(still->length * 1000 / 30));
          dvdnav_still_skip(mDvdNav);
        }
        continue;
      }

      case DVDNAV_WAIT:
        dvdnav_wait_skip(mDvdNav);
        continue;

      case DVDNAV_VTS_CHANGE:
      case DVDNAV_CELL_CHANGE:
      case DVDNAV_HOP_CHANNEL:
        if (mAvCtx) avcodec_flush_buffers(mAvCtx);
        continue;

      case DVDNAV_HIGHLIGHT: {
        const dvdnav_highlight_event_t* hl =
            (const dvdnav_highlight_event_t*)navBuf;
        mCurrentButton.store(hl->buttonN, std::memory_order_release);
        continue;
      }

      case DVDNAV_STOP:
        fprintf(stderr, "[DVD] DVDNAV_STOP\n");
        return 0;

      case DVDNAV_SPU_CLUT_CHANGE:
      case DVDNAV_SPU_STREAM_CHANGE:
      case DVDNAV_AUDIO_STREAM_CHANGE:
      case DVDNAV_NOP:
      default:
        continue;
    }
  }
  return 0;
}
#else
void DvdPlayer::ApplyPendingButton() {}
int DvdPlayer::AvioReadDvd(void*, uint8*, int) { return 0; }
int DvdPlayer::ReadDvdBlock(uint8*, int) { return 0; }
#endif
