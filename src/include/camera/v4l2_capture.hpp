/**
 * @file    v4l2_capture.hpp
 * @brief   V4L2 mmap + 常時STREAMONによる高速フレーム取得
 * @author  sawada souta
 * @date    2025-12-18
 */

#ifndef V4L2_CAPTURE_HPP_
#define V4L2_CAPTURE_HPP_

#include <cstdint>
#include <string>
#include <vector>


class V4L2Capture {
public:
    struct Frame {
        uint8_t* data = nullptr;
        uint32_t size = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t fourcc = 0;
        int buffer_index = -1;

        Frame() = default;

        // コピー禁止
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

        // ムーブのみ許可
        Frame(Frame&& other) noexcept
        {
            *this = std::move(other);
        }

        Frame& operator=(Frame&& other) noexcept
        {
            data = other.data;
            size = other.size;
            width = other.width;
            height = other.height;
            fourcc = other.fourcc;
            buffer_index = other.buffer_index;

            other.data = nullptr;
            other.buffer_index = -1;
            return *this;
        }
    };

    V4L2Capture(const std::string& device_name,
                uint32_t width,
                uint32_t height);

    ~V4L2Capture();

    bool initialize();

    bool get_once_frame(Frame& frame);
    void release_frame(Frame& frame);

private:
    bool open_device();
    void close_device();
    bool set_frame_format(uint32_t width, uint32_t height, uint32_t fourcc);

    struct Buffer {
        void*  start = nullptr;
        size_t length = 0;
    };

    std::string device_name_;
    int device_fd_{-1};
    uint32_t width_;
    uint32_t height_;
    std::vector<Buffer> buffers_;
};

#endif
