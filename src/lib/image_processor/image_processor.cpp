/**
 * @file    image_processor.cpp
 * @brief   画像処理クラス実装 (YUYV → GUI / AI)
 * @author  sawada souta
 * @date    2025-12-14
 */

#include "image_processor/image_processor.hpp"

#include <cstring>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

/* ================================
 * コンストラクタ / デストラクタ
 * ================================ */

ImageProcessor::ImageProcessor(uint8_t quality, double resize_width)
    : quality_(quality),
      contrast_alpha_(1.0),
      brightness_beta_(0.0),
      resize_width_(resize_width),
      tj_enc_(nullptr),
      jpeg_buffer_()
{
    if (quality_ < 1 || quality_ > 100) {
        quality_ = 80;
    }

    if (resize_width_ <= 0.0) {
        resize_width_ = 640.0;
    }

    tj_enc_ = tjInitCompress();
    if (!tj_enc_) {
        throw std::runtime_error("tjInitCompress failed");
    }
}

ImageProcessor::~ImageProcessor()
{
    if (tj_enc_) {
        tjDestroy(tj_enc_);
    }
}

bool ImageProcessor::process_for_gui(const uint8_t* yuyv,
                                     uint32_t width,
                                     uint32_t height,
                                     GuiProcessedData& output)
{
    if (!yuyv || width == 0 || height == 0) {
        return false;
    }

    cv::Mat bgr;

    /* YUYV → BGR */
    if (!yuyv_to_bgr(yuyv, width, height, bgr)) {
        return false;
    }

    /* リサイズ（必要な場合のみ） */
    if (resize_width_ > 0 && bgr.cols != static_cast<int>(resize_width_)) {
        double scale = resize_width_ / static_cast<double>(bgr.cols);
        cv::resize(bgr, bgr, cv::Size(), scale, scale, cv::INTER_LINEAR);
    }

    /* コントラスト・明るさ補正 */
    bgr.convertTo(bgr, -1, contrast_alpha_, brightness_beta_);

    /* JPEGエンコード */
    if (!encode_to_jpeg(bgr, output.encoded_jpeg)) {
        return false;
    }

    output.bgr_mat = bgr;

    return true;
}

bool ImageProcessor::process_for_ai(const cv::Mat& bgr_img,
                                    AiProcessedData& output)
{
    if (bgr_img.empty()) {
        return false;
    }

    /* グレースケール化 */
    cv::cvtColor(bgr_img, output.gray_mat, cv::COLOR_BGR2GRAY);

    /* 2値化（Otsu） */
    cv::threshold(output.gray_mat,
                  output.binary_mat,
                  0,
                  255,
                  cv::THRESH_BINARY | cv::THRESH_OTSU);

    return true;
}

void ImageProcessor::setContrast(double alpha, double beta)
{
    contrast_alpha_ = alpha;
    brightness_beta_ = beta;
}

bool ImageProcessor::yuyv_to_bgr(const uint8_t* yuyv,
                                 uint32_t width,
                                 uint32_t height,
                                 cv::Mat& bgr)
{
    if (!yuyv) {
        return false;
    }

    /*
     * YUYV (YUY2) フォーマット
     * [Y0 U Y1 V] が 2pixel 単位で並ぶ
     */

    cv::Mat yuyv_mat(height, width, CV_8UC2, const_cast<uint8_t*>(yuyv));

    cv::cvtColor(yuyv_mat, bgr, cv::COLOR_YUV2BGR_YUY2);

    return true;
}

bool ImageProcessor::encode_to_jpeg(const cv::Mat& bgr,
                                    std::vector<uint8_t>& encoded)
{
    if (bgr.empty()) {
        return false;
    }

    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;

    int ret = tjCompress2(
        tj_enc_,
        bgr.data,
        bgr.cols,
        0,
        bgr.rows,
        TJPF_BGR,
        &jpeg_buf,
        &jpeg_size,
        TJSAMP_420,
        quality_,
        TJFLAG_FASTDCT
    );

    if (ret != 0) {
        return false;
    }

    encoded.assign(jpeg_buf, jpeg_buf + jpeg_size);
    tjFree(jpeg_buf);

    return true;
}
