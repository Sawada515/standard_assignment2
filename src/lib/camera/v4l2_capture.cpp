/**
 * @file    v4l2_capture.cpp
 * @brief   Webカメラから画像データの取得
 * @author  sawada souta
 * @version 0.2
 * @date    2025-12-16
 * @note    YUYVフォーマットのデータを取得
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <vector>
#include <iostream> 

#include "camera/v4l2_capture.hpp"
#include "logger/logger.hpp"

static inline int xioctl(int fd, unsigned long request, void *arg)
{
    int r;

    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

V4L2Capture::V4L2Capture(const std::string& device_name, uint32_t width, uint32_t height)
    : device_name_(""),
      device_fd_(-1),
      buffers_()
{
    device_name_ = device_name;
    width_ = width;
    height_ = height;

    LOG_I("V4L2Capture constructor called");
}

V4L2Capture::~V4L2Capture()
{
    close_device();

    LOG_I("V4L2Capture destructor called");
}

bool V4L2Capture::open_device(void)
{
    device_fd_ = ::open(device_name_.c_str(), O_RDWR | O_NONBLOCK, 0);

    if (device_fd_ < 0) {
        LOG_E("Failed to open device %s: %s", device_name_.c_str(), std::strerror(errno));

        return false;
   }

    LOG_I("Device %s opened successfully", device_name_.c_str());

    v4l2_format fmt{};
    std::memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(device_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_E("Failed to set format: %s", std::strerror(errno));

        return false;
    }

    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;

    set_fps(10);

    v4l2_requestbuffers req{};

    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOG_E("Failed to request buffers: %s", std::strerror(errno));

        return false;
    }

    buffers_.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        std::memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_E("Failed to query buffer %d: %s", i,  std::strerror(errno));

            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED) {
            LOG_E("Failed to mmap buffer %d: %s", i, std::strerror(errno));

            return false;
        }

        if (xioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
            LOG_E("Failed to queue buffer %d: %s", i, std::strerror(errno));

            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(device_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG_E("Failed to start streaming: %s", std::strerror(errno));

        close_device();

        return false;
    }

    return true;
}

bool V4L2Capture::get_frame(V4L2Capture::Frame& frame)
{
    if (device_fd_ < 0) {
        LOG_E("Device not opened");

        return false;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(device_fd_, &fds);

    timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int r = select(device_fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (r == 0) {
        return false;
    }
    if (r < 0) {
        LOG_E("Select error: %s", std::strerror(errno));

        return false;
    }

    v4l2_buffer buf{};
    std::memset(&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) {
            return false;
        } else {
            LOG_E("Failed to dequeue buffer: %s", std::strerror(errno));

            return false;
        }
    }

    frame.data = static_cast<uint8_t*>(buffers_[buf.index].start);
    frame.length = buf.bytesused;
    frame.v4l2_queue_index = buf.index;
    frame.owner = this;

    return true;
}

bool V4L2Capture::release_frame(V4L2Capture::Frame& frame)
{
    v4l2_buffer buf{};

    if (device_fd_ < 0) {
        LOG_E("Device not opened");

        return false;
    }

    if (frame.data == nullptr) {
        LOG_E("Frame data is null");

        return false;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame.v4l2_queue_index;

    if (xioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
        LOG_E("Failed to re-queue buffer: %s", std::strerror(errno));
        
        return false;
    }

    frame.data = nullptr;
    frame.length = 0;
    frame.v4l2_queue_index = -1;
    frame.owner = nullptr;

    return true;
}

void V4L2Capture::close_device()
{
    if (device_fd_ < 0) {
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(device_fd_, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_E("Failed to stop streaming: %s", std::strerror(errno));
    }

    for (auto& buf : buffers_) {
        if (munmap(buf.start, buf.length) < 0) {
            LOG_E("Failed to unmap buffer: %s", std::strerror(errno));
        }
    }

    buffers_.clear();

    close(device_fd_);

    device_fd_ = -1;
}

bool V4L2Capture::set_fps(uint32_t fps)
{
    if (device_fd_ < 0) {
        LOG_E("Device not opened");

        return false;
    }

    v4l2_streamparm streamparm{};
    std::memset(&streamparm, 0, sizeof(streamparm));

    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(device_fd_, VIDIOC_G_PARM, &streamparm) < 0) {
        LOG_E("Failed to get stream parameters: %s", std::strerror(errno));

        return false;
    }

    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = fps;

    if (xioctl(device_fd_, VIDIOC_S_PARM, &streamparm) < 0) {
        LOG_E("Failed to set stream parameters: %s", std::strerror(errno));

        return false;
    }

    LOG_I("FPS set to %u", fps);

    return true;
}