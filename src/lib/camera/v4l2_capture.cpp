/**
 * @file	v4l2_capture.cpp
 * @brief	Webカメラから画像データの取得
 * @author	sawada souta
 * @version 0.3
 * @date	2025-12-17
 * @note	YUYVフォーマットの画像データを1枚取得（mmap版）
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
    if (!open_device()) {
        return false;
    }

    if (!set_frame_format(width_, height_, V4L2_PIX_FMT_YUYV)) {
        close_device();
        return false;
    }

    // バッファを1つだけ要求（メモリ効率のため）
    v4l2_requestbuffers req{};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOG_E("VIDIOC_REQBUFS failed: %s", strerror(errno));
        close_device();
        return false;
    }

    // バッファ情報を取得
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    if (xioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
        LOG_E("VIDIOC_QUERYBUF failed: %s", strerror(errno));
        close_device();
        return false;
    }

    // メモリマップ
    void* buffer_start = mmap(NULL, buf.length,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED,
                              device_fd_, buf.m.offset);

    if (buffer_start == MAP_FAILED) {
        LOG_E("mmap failed: %s", strerror(errno));
        close_device();
        return false;
    }

    // バッファをキューに追加
    v4l2_buffer qbuf{};
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = 0;

    if (xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0) {
        LOG_E("VIDIOC_QBUF failed: %s", strerror(errno));
        munmap(buffer_start, buf.length);
        close_device();
        return false;
    }

    // ストリーミング開始
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(device_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG_E("VIDIOC_STREAMON failed: %s", strerror(errno));
        munmap(buffer_start, buf.length);
        close_device();
        return false;
    }

    // バッファを取得（1フレーム取得）
    v4l2_buffer dqbuf{};
    dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dqbuf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_DQBUF, &dqbuf) < 0) {
        LOG_E("VIDIOC_DQBUF failed: %s", strerror(errno));
        xioctl(device_fd_, VIDIOC_STREAMOFF, &type);
        munmap(buffer_start, buf.length);
        close_device();
        return false;
    }

    // データをコピー
    frame.data.resize(dqbuf.bytesused);
    memcpy(frame.data.data(), buffer_start, dqbuf.bytesused);

    frame.width  = width_;
    frame.height = height_;
    frame.fourcc = V4L2_PIX_FMT_YUYV;

    // ストリーミング停止
    xioctl(device_fd_, VIDIOC_STREAMOFF, &type);

    munmap(buffer_start, buf.length);

    close_device();

    return true;
}
