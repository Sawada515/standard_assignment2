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
#include <arm_neon.h>

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
    const uint32_t pixel_count = width * height;
    bgr.resize(pixel_count * 3);

    const uint8_t* src = yuyv;
    uint8_t* dst = bgr.data();

    constexpr int16_t C_RV = 359; // 1.402 * 256
    constexpr int16_t C_GU = 88;  // 0.344 * 256
    constexpr int16_t C_GV = 183; // 0.714 * 256
    constexpr int16_t C_BU = 454; // 1.772 * 256

    uint32_t i = 0;
    
#ifdef __aarch64__
    // NEON版：8ピクセル（16バイトYUYV）ずつ処理
    for (; i + 8 <= pixel_count; i += 8) {
        // 手動でY, U, Vを抽出（最も確実な方法）
        uint8_t y_arr[8], u_arr[8], v_arr[8];
        
        // YUYVから抽出: Y0 U0 Y1 V0 Y2 U1 Y3 V1 ...
        y_arr[0] = src[0];  u_arr[0] = src[1];  u_arr[1] = src[1];
        y_arr[1] = src[2];  v_arr[0] = src[3];  v_arr[1] = src[3];
        y_arr[2] = src[4];  u_arr[2] = src[5];  u_arr[3] = src[5];
        y_arr[3] = src[6];  v_arr[2] = src[7];  v_arr[3] = src[7];
        y_arr[4] = src[8];  u_arr[4] = src[9];  u_arr[5] = src[9];
        y_arr[5] = src[10]; v_arr[4] = src[11]; v_arr[5] = src[11];
        y_arr[6] = src[12]; u_arr[6] = src[13]; u_arr[7] = src[13];
        y_arr[7] = src[14]; v_arr[6] = src[15]; v_arr[7] = src[15];
        
        src += 16;
        
        // ベクトルにロード
        uint8x8_t y_vals = vld1_u8(y_arr);
        uint8x8_t u_vals = vld1_u8(u_arr);
        uint8x8_t v_vals = vld1_u8(v_arr);

        // 16bitに拡張
        int16x8_t y16 = vreinterpretq_s16_u16(vmovl_u8(y_vals));
        int16x8_t u16 = vreinterpretq_s16_u16(vmovl_u8(u_vals));
        int16x8_t v16 = vreinterpretq_s16_u16(vmovl_u8(v_vals));

        // 中心化
        int16x8_t offset = vdupq_n_s16(128);
        u16 = vsubq_s16(u16, offset);
        v16 = vsubq_s16(v16, offset);

        // RGB計算
        int16x8_t c_rv_vec = vdupq_n_s16(C_RV);
        int16x8_t c_gu_vec = vdupq_n_s16(C_GU);
        int16x8_t c_gv_vec = vdupq_n_s16(C_GV);
        int16x8_t c_bu_vec = vdupq_n_s16(C_BU);
        
        int16x8_t r = vaddq_s16(y16, vshrq_n_s16(vmulq_s16(v16, c_rv_vec), 8));
        int16x8_t g = vsubq_s16(y16, vshrq_n_s16(vaddq_s16(vmulq_s16(u16, c_gu_vec), 
                                                            vmulq_s16(v16, c_gv_vec)), 8));
        int16x8_t b = vaddq_s16(y16, vshrq_n_s16(vmulq_s16(u16, c_bu_vec), 8));

        // 飽和キャスト
        uint8x8_t r8 = vqmovun_s16(r);
        uint8x8_t g8 = vqmovun_s16(g);
        uint8x8_t b8 = vqmovun_s16(b);

        // BGRで保存
        uint8x8x3_t bgr_pack;
        bgr_pack.val[0] = b8;
        bgr_pack.val[1] = g8;
        bgr_pack.val[2] = r8;

        vst3_u8(dst, bgr_pack);
        dst += 24;
    }
#endif

    // 残りのピクセル
    for (; i < pixel_count; i += 2) {
        int y0 = src[0];
        int u  = src[1] - 128;
        int y1 = src[2];
        int v  = src[3] - 128;
        src += 4;

        int ru = (C_RV * v) >> 8;
        int gu = (C_GU * u + C_GV * v) >> 8;
        int bu = (C_BU * u) >> 8;

        int r0 = y0 + ru;
        int g0 = y0 - gu;
        int b0 = y0 + bu;

        dst[0] = (b0 < 0) ? 0 : ((b0 > 255) ? 255 : b0);
        dst[1] = (g0 < 0) ? 0 : ((g0 > 255) ? 255 : g0);
        dst[2] = (r0 < 0) ? 0 : ((r0 > 255) ? 255 : r0);
        dst += 3;

        int r1 = y1 + ru;
        int g1 = y1 - gu;
        int b1 = y1 + bu;

        dst[0] = (b1 < 0) ? 0 : ((b1 > 255) ? 255 : b1);
        dst[1] = (g1 < 0) ? 0 : ((g1 > 255) ? 255 : g1);
        dst[2] = (r1 < 0) ? 0 : ((r1 > 255) ? 255 : r1);
        dst += 3;
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
