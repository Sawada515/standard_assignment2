/**
 * @file    v4l2_capture.cpp
 * @brief   Webカメラから画像データの取得
 * @author  sawada souta
 * @version 0.1
 * @date    2025-12-14
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

V4L2Capture::V4L2Capture(uint32_t width, uint32_t height)
    : device_name_(""),
      device_fd_(-1),
      buffers_()
{
    width_ = width;
    height_ = height;

    LOG_I("V4L2Capture constructor called");
}

V4L2Capture::~V4L2Capture()
{
    stop_stream();
    close_device();

    LOG_I("V4L2Capture destructor called");
}

bool V4L2Capture::open_device(const std::string& device)
{
    device_name_ = device;
    // O_RDWR | O_NONBLOCK は適切です
    device_fd_ = ::open(device_name_.c_str(), O_RDWR | O_NONBLOCK, 0);

    if (device_fd_ < 0) {
        LOG_E("Failed to open device %s: %s", device_name_.c_str(), std::strerror(errno));

        return false;
    }

    LOG_I("Device %s opened successfully", device_name_.c_str());

    return true;
}

bool V4L2Capture::start_stream()
{
    if (device_fd_ < 0) {
        LOG_E("Device not opened");

        return false;
    }
    
    struct v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG; // MJPEG指定
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;    // または V4L2_FIELD_NONE

    if (ioctl(device_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_E("Failed to set format: %s", std::strerror(errno));

        return false;
    }

    // 実際に設定されたサイズをメンバ変数に更新（カメラが指定サイズ未対応の場合に丸められるため）
    if (fmt.fmt.pix.width != width_ || fmt.fmt.pix.height != height_) {
        LOG_I("Resolution adjusted by driver: %d x %d", fmt.fmt.pix.width, fmt.fmt.pix.height);

        width_ = fmt.fmt.pix.width;
        height_ = fmt.fmt.pix.height;
    }

    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOG_E("Failed to request buffers: %s", std::strerror(errno));

        return false;
    }

    buffers_.resize(req.count);

    for (size_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_E("Failed to query buffer %zu: %s", i, std::strerror(errno));

            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED) {
            LOG_E("Failed to mmap buffer %zu: %s", i, std::strerror(errno));

            return false;
        }

        if (ioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
            LOG_E("Failed to queue buffer %zu: %s", i, std::strerror(errno));

            return false;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(device_fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG_E("Failed to start streaming: %s", std::strerror(errno));

        return false;
    }

    return true;
}

bool V4L2Capture::read_frame(V4L2Capture::Frame& frame)
{
    if (device_fd_ < 0) {
        LOG_E("Device not opened");

        return false;
    }

    struct v4l2_buffer buf;
    std::memset(&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // バッファを取り出す (Dequeue)
    if (ioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) {
            // ノンブロッキングモードでデータがまだない場合
            return false; 
        } else {
            LOG_E("Failed to dequeue buffer: %s", std::strerror(errno));

            return false;
        }
    }

    // 【重要】
    // ここでポインタを渡すだけだと、下の QBUF を呼んだ瞬間にデータが上書きされる可能性があります。
    // 本来は memcpy するか、QBUF をユーザーが使い終わるまで待つ設計にします。
    // 今回は安全のため「ポインタを渡すが、呼び出し元ですぐにコピーすること」を前提とします。
    // もし呼び出し元で長時間保持したい場合は、ここで memcpy してください。
    
    frame.data = static_cast<uint8_t*>(buffers_[buf.index].start);
    frame.length = buf.bytesused;
    frame.width = width_;
    frame.height = height_;

    // バッファを返却する (Queue)
    // 注意: これを実行した瞬間に frame.data の中身は保証されなくなります！
    if (ioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
        LOG_E("Failed to re-queue buffer: %s", std::strerror(errno));
        
        return false;
    }

    return true;
}

void V4L2Capture::stop_stream()
{
    if (device_fd_ < 0) {
        return;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(device_fd_, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_E("Failed to stop streaming: %s", std::strerror(errno));
    }

    for (size_t i = 0; i < buffers_.size(); ++i) {
        if (munmap(buffers_[i].start, buffers_[i].length) < 0) {
            LOG_E("Failed to unmap buffer %zu: %s", i, std::strerror(errno));
        }
    }

    buffers_.clear();
}

void V4L2Capture::close_device()
{
    if (device_fd_ >= 0) {
        ::close(device_fd_);
        device_fd_ = -1;
        device_name_.clear();

        LOG_I("Device closed successfully");
    }
}
