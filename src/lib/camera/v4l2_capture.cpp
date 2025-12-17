/**
 * @file    v4l2_capture.cpp
 * @brief   Webカメラから画像データの取得
 * @author  sawada souta
 * @version 0.4
 * @date    2025-12-17
 * @note    YUYVフォーマットの画像データを1枚取得（mmap版・高速化対応）
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cstring>
#include <cerrno>

#include "camera/v4l2_capture.hpp"
#include "logger/logger.hpp"

static int xioctl(int fd, unsigned long req, void* arg)
{
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Capture::V4L2Capture(const std::string& device_name,
                         uint32_t width,
                         uint32_t height)
    : device_name_(device_name),
      device_fd_(-1),
      width_(width),
      height_(height)
{
}

V4L2Capture::~V4L2Capture()
{
    close_device();
}

// 追加: ループ前に呼ぶ初期化関数
bool V4L2Capture::initialize()
{
    if (device_fd_ >= 0) {
        return true; // 既にOpen済み
    }

    // 1. デバイスOpen
    if (!open_device()) {
        return false;
    }

    // 2. フォーマット設定
    if (!set_frame_format(width_, height_, V4L2_PIX_FMT_YUYV)) {
        close_device();
        return false;
    }

    // 3. バッファ要求 (REQBUFS)
    v4l2_requestbuffers req{};
    req.count = 1; // バッファ数は最小限に
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOG_E("VIDIOC_REQBUFS failed: %s", strerror(errno));
        close_device();
        return false;
    }

    // 4. メモリマップ (QUERYBUF & mmap)
    buffers_.resize(req.count);
    for (size_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_E("VIDIOC_QUERYBUF failed: %s", strerror(errno));
            close_device();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(NULL, buf.length,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 device_fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED) {
            LOG_E("mmap failed: %s", strerror(errno));
            close_device();
            return false;
        }
    }

    return true;
}

bool V4L2Capture::open_device()
{
    device_fd_ = ::open(device_name_.c_str(), O_RDWR);
    if (device_fd_ < 0) {
        LOG_E("open failed: %s", strerror(errno));
        return false;
    }
    return true;
}

void V4L2Capture::close_device()
{
    if (device_fd_ >= 0) {
        // ストリーミング停止（念のため）
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(device_fd_, VIDIOC_STREAMOFF, &type);

        // mmap解除
        for (auto& buf : buffers_) {
            if (buf.start != MAP_FAILED) {
                munmap(buf.start, buf.length);
            }
        }
        buffers_.clear();

        // クローズ
        ::close(device_fd_);
        device_fd_ = -1;
    }
}

bool V4L2Capture::set_frame_format(uint32_t width, uint32_t height, uint32_t fourcc)
{
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(device_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_E("VIDIOC_S_FMT failed: %s", strerror(errno));
        return false;
    }

    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;

    return true;
}

bool V4L2Capture::get_once_frame(Frame& frame)
{
    // 初期化されていなければ初期化する (安全策)
    if (device_fd_ < 0) {
        if (!initialize()) {
            return false;
        }
    }

    // 1. バッファをキューに追加 (QBUF)
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0; // index 0 を使用

    if (xioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
        LOG_E("VIDIOC_QBUF failed: %s", strerror(errno));
        return false;
    }

    // 2. ストリーミング開始 (ここでUSB帯域を確保) 
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(device_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG_E("VIDIOC_STREAMON failed: %s", strerror(errno));
        return false;
    }

    // 3. バッファを取得 (DQBUF)
    if (xioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0) {
        LOG_E("VIDIOC_DQBUF failed: %s", strerror(errno));
        // エラー時もストリームは止める
        xioctl(device_fd_, VIDIOC_STREAMOFF, &type);
        return false;
    }

    // 4. データをコピー
    if (buf.index < buffers_.size()) {
        frame.data.resize(buf.bytesused);
        memcpy(frame.data.data(), buffers_[buf.index].start, buf.bytesused);
        
        frame.width  = width_;
        frame.height = height_;
        frame.fourcc = V4L2_PIX_FMT_YUYV;
    }

    // 5. ストリーミング停止 (USB帯域を開放)
    if (xioctl(device_fd_, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_E("VIDIOC_STREAMOFF failed: %s", strerror(errno));
        return false;
    }

    // ※ close_device() はここでは呼ばない！

    return true;
}
