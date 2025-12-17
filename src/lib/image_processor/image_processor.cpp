/**
 * @file    image_processor.cpp
 * @brief   画像処理（負荷軽減版）
 * @author  sawada souta
 * @date    2025-12-17
 */

#include <cstring>
#include <algorithm>
#include <vector>
#include <turbojpeg.h>
#include <arm_neon.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

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

    /* ---------- 1. OpenCVを使った高速 YUYV -> BGR 変換 ---------- */
    
    // 出力先ベクタのサイズ確保 (再確保はサイズ変更時のみ発生)
    size_t bgr_size = width * height * 3;
    if (ai_data.image.size() != bgr_size) {
        ai_data.image.resize(bgr_size);
    }

    // OpenCVのMatラッパーを作成（データコピーは発生しません）
    // YUYVは2バイト/画素なので、CV_8UC2 として扱います
    cv::Mat src_mat(height, width, CV_8UC2, (void*)yuyv);
    
    // 出力先もMatでラップ（ベクタのメモリを直接使う）
    cv::Mat dst_mat(height, width, CV_8UC3, ai_data.image.data());

    // 変換実行 (内部でNEON最適化が働きます)
    // カメラによっては COLOR_YUV2BGR_YUYV ではなく COLOR_YUV2BGR_UYVY の場合があるので
    // もし色が変ならここを変えてみてください。
    cv::cvtColor(src_mat, dst_mat, cv::COLOR_YUV2BGR_YUYV);

    // AI用データ情報セット
    ai_data.width    = width;
    ai_data.height   = height;
    ai_data.channels = 3;

    /* ---------- 2. GUI用: JPEG圧縮 (TurboJPEG) ---------- */
    // OpenCVで作ったきれいなBGRデータを使って圧縮
    if (!bgr_to_jpeg(ai_data.image, width, height, jpeg_quality_, gui_data.image)) {
        return false;
    }
    
    gui_data.width = width;
    gui_data.height = height;
    gui_data.is_jpeg = true;

    return true;
}

bool ImageProcessor::bgr_to_jpeg(const std::vector<uint8_t>& bgr,
                                 uint32_t width,
                                 uint32_t height,
                                 int quality,
                                 std::vector<uint8_t>& jpeg)
{
    if (bgr.empty() || width == 0 || height == 0) return false;

    tjhandle tj_instance = tjInitCompress();
    if (!tj_instance) return false;

    unsigned char* outbuf = nullptr;
    unsigned long outsize = 0;

    int ret = tjCompress2(
        tj_instance,
        bgr.data(),
        width,
        0,              // pitch
        height,
        TJPF_BGR,       // 入力はBGR
        &outbuf,
        &outsize,
        TJSAMP_444,     // 4:4:4 サブサンプリング
        quality,
        //TJFLAG_FASTDCT
        TJFLAG_ACCURATEDCT
    );

    tjDestroy(tj_instance);

    if (ret != 0) return false;

    jpeg.assign(outbuf, outbuf + outsize);
    tjFree(outbuf);

    return true;
}
