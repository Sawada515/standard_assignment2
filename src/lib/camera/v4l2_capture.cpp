/**
 * @file    v4l2_capture.cpp
 * @brief   V4L2 mmap キャプチャ実装（常時STREAMON）
 * @author  sawada souta
 * @date    2025-12-18
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
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
      width_(width),
      height_(height)
{
}

V4L2Capture::~V4L2Capture()
{
    close_device();
}
bool V4L2Capture::initialize()
{
    if (device_fd_ >= 0) {
        return true;
    }

    if (!open_device()) {
        return false;
    }

    if (!set_frame_format(width_, height_, V4L2_PIX_FMT_YUYV)) {
        close_device();

        return false;
    }

    v4l2_requestbuffers req{};
    req.count  = 2;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOG_E("VIDIOC_REQBUFS failed: %s", strerror(errno));

        close_device();

        return false;
    }

    buffers_.resize(req.count);

    for (size_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_E("VIDIOC_QUERYBUF failed: %s", strerror(errno));

            close_device();

            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr,
                                 buf.length,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 device_fd_,
                                 buf.m.offset);

        if (buffers_[i].start == MAP_FAILED) {
            LOG_E("mmap failed: %s", strerror(errno));

            close_device();

            return false;
        }

        // ★ QBUF はここで1回だけ
        if (xioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
            LOG_E("VIDIOC_QBUF(init) failed: %s", strerror(errno));

            close_device();

            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(device_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG_E("VIDIOC_STREAMON failed: %s", strerror(errno));

        close_device();

        return false;
    }

    return true;
}


bool V4L2Capture::open_device()
{
    device_fd_ = ::open(device_name_.c_str(), O_RDWR | O_NONBLOCK);
    if (device_fd_ < 0) {
        LOG_E("open failed: %s", strerror(errno));

        return false;
    }
    return true;
}

void V4L2Capture::close_device()
{
    if (device_fd_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(device_fd_, VIDIOC_STREAMOFF, &type);

        for (auto& buf : buffers_) {
            if (buf.start) {
                munmap(buf.start, buf.length);
            }
        }
        buffers_.clear();

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
    pollfd pfd{};
    pfd.fd = device_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 1000);
    if (ret <= 0) {
        return false;
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0) {
        return false;
    }

    frame.data = static_cast<uint8_t*>(buffers_[buf.index].start);
    frame.size = buf.bytesused;
    frame.width = width_;
    frame.height = height_;
    frame.fourcc = V4L2_PIX_FMT_YUYV;
    frame.buffer_index = buf.index;

    return true;
}

void V4L2Capture::release_frame(Frame& frame)
{
    if (frame.buffer_index < 0) {
        return;
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame.buffer_index;

    xioctl(device_fd_, VIDIOC_QBUF, &buf);

    frame.buffer_index = -1;
    frame.data = nullptr;
}
