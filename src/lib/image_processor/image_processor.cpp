/**
 * @file    image_processor.cpp
 * @brief   画像処理（YUYV入力 → GUI / AI 出力）
 * @author  sawada souta
 * @date    2025-12-16
 */

#include <cstring>
#include <algorithm>
#include <vector>
#include <turbojpeg.h>

#include "image_processor/image_processor.hpp"
#include "logger/logger.hpp"

ImageProcessor::ImageProcessor(int jpeg_quality, uint32_t resize_width) :
    jpeg_quality_(jpeg_quality),
    resize_width_(resize_width)
{
}

ImageProcessor::~ImageProcessor() = default;

bool ImageProcessor::process_frame(const uint8_t* yuyv,
                                   uint32_t width,
                                   uint32_t height,
                                   GuiProcessedData& gui_data,
                                   AiProcessedData& ai_data)
{
    if (!yuyv || width == 0 || height == 0) {
        return false;
    }

    /* ---------- AI用（BGR） ---------- */
    yuyv_to_bgr(yuyv, width, height, ai_data.image);
    ai_data.width  = width;
    ai_data.height = height;
    ai_data.channels = 3;

    /* ---------- GUI用 JPEG（TurboJPEG） ---------- */
    if (!yuyv_to_jpeg_debug(yuyv, width, height, jpeg_quality_, gui_data.image)) {
        return false;
    }
    gui_data.width = width;
    gui_data.height = height;
    gui_data.is_jpeg = true;

    return true;
}

void ImageProcessor::yuyv_to_bgr(const uint8_t* yuyv,
                                 uint32_t width,
                                 uint32_t height,
                                 std::vector<uint8_t>& bgr)
{
    bgr.resize(width * height * 3);

    for (uint32_t i = 0, j = 0; i < width * height * 2; i += 4) {
        int y0 = yuyv[i + 0];
        int u  = yuyv[i + 1] - 128;
        int y1 = yuyv[i + 2];
        int v  = yuyv[i + 3] - 128;

        auto convert = [&](int y, int& r, int& g, int& b) {
            r = y + 1.402 * v;
            g = y - 0.344 * u - 0.714 * v;
            b = y + 1.772 * u;
            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);
        };

        int r, g, b;
        convert(y0, r, g, b);
        bgr[j++] = b;
        bgr[j++] = g;
        bgr[j++] = r;

        convert(y1, r, g, b);
        bgr[j++] = b;
        bgr[j++] = g;
        bgr[j++] = r;
    }
}

void ImageProcessor::extract_resistor_candidates(const uint8_t* yuyv,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 std::vector<uint8_t>& binary)
{
    binary.resize(width * height);

    for (uint32_t i = 0, p = 0; i < width * height * 2; i += 2, ++p) {
        uint8_t y = yuyv[i];
        binary[p] = (y > 120) ? 255 : 0;
    }
}

bool ImageProcessor::yuyv_to_jpeg_debug(const uint8_t* yuyv,
                                        uint32_t width,
                                        uint32_t height,
                                        int quality,
                                        std::vector<uint8_t>& jpeg)
{
    if (!yuyv || width == 0 || height == 0) return false;

    std::vector<uint8_t> bgr;
    yuyv_to_bgr(yuyv, width, height, bgr);

    tjhandle tj_instance = tjInitCompress();
    if (!tj_instance) return false;

    unsigned char* outbuf = nullptr;
    unsigned long outsize = 0;

    int ret = tjCompress2(
        tj_instance,
        bgr.data(),
        width,
        0,              // pitch = 0 -> width * 3
        height,
        TJPF_BGR,       // 入力フォーマット
        &outbuf,
        &outsize,
        TJSAMP_420,     // サブサンプリング
        quality,
        TJFLAG_FASTDCT  // 高速圧縮
    );

    tjDestroy(tj_instance);

    if (ret != 0) return false;

    jpeg.assign(outbuf, outbuf + outsize);
    tjFree(outbuf);

    return true;
}

void ImageProcessor::resize_for_gui(const std::vector<uint8_t>& src,
                                    uint32_t src_w,
                                    uint32_t src_h,
                                    uint32_t dst_w,
                                    uint32_t dst_h,
                                    std::vector<uint8_t>& dst)
{
    if (src.empty() || src_w == 0 || src_h == 0 ||
        dst_w == 0 || dst_h == 0) {
        dst.clear();
        return;
    }

    constexpr uint32_t CHANNELS = 3; // BGR

    dst.resize(dst_w * dst_h * CHANNELS);

    const float scale_x = static_cast<float>(src_w) / dst_w;
    const float scale_y = static_cast<float>(src_h) / dst_h;

    for (uint32_t y = 0; y < dst_h; ++y) {
        uint32_t src_y = static_cast<uint32_t>(y * scale_y);
        if (src_y >= src_h) src_y = src_h - 1;

        for (uint32_t x = 0; x < dst_w; ++x) {
            uint32_t src_x = static_cast<uint32_t>(x * scale_x);
            if (src_x >= src_w) src_x = src_w - 1;

            const uint32_t src_idx =
                (src_y * src_w + src_x) * CHANNELS;
            const uint32_t dst_idx =
                (y * dst_w + x) * CHANNELS;

            dst[dst_idx + 0] = src[src_idx + 0]; // B
            dst[dst_idx + 1] = src[src_idx + 1]; // G
            dst[dst_idx + 2] = src[src_idx + 2]; // R
        }
    }
}
