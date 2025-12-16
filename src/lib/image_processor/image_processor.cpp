/**
 * @file    image_processor.cpp
 * @brief   画像処理クラスの実装 (判定緩和・デバッグログ強化版)
 * @author  sawada souta
 * @date    2025-12-16
 */

#include <vector>
#include <turbojpeg.h>
#include "image_processor/image_processor.hpp"
#include "logger/logger.hpp"

// コンストラクタ
ImageProcessor::ImageProcessor(uint8_t quality, double resize_width)
    : quality_(quality),
      contrast_alpha_(1.0),
      brightness_beta_(0.0),
      resize_width_(resize_width),
      tj_dec_(nullptr),
      bgr_buffer_(),
      tj_enc_(nullptr),
      jpg_buffer_()
{
    quality_ = (quality <= 0 || quality > 100) ? 80 : quality;
    resize_width_ = (resize_width_ <= 0.0) ? 640.0 : resize_width_;

    cv::setNumThreads(2);
    cv::setUseOptimized(true);

    tj_dec_ = tjInitDecompress();
    if (tj_dec_ == nullptr) {
        LOG_E("Failed to initialize TurboJPEG decompressor: %s", tjGetErrorStr());
        exit(-1);
    }
    
    tj_enc_ = tjInitCompress();
    if (tj_enc_ == nullptr) {
        LOG_E("Failed to initialize TurboJPEG compressor: %s", tjGetErrorStr());
    }
}

// デストラクタ
ImageProcessor::~ImageProcessor()
{
    if (tj_dec_ != nullptr) {
        tjDestroy(tj_dec_);
    }
    if (tj_enc_ != nullptr) {
        tjDestroy(tj_enc_);
    }
}

// コントラストと明るさの設定
void ImageProcessor::setContrast(double alpha, double beta)
{
    contrast_alpha_ = alpha;
    brightness_beta_ = beta;
}

// GUI用（標準）処理パイプライン
bool ImageProcessor::process_for_gui(const V4L2Capture::Frame& frame, StdProcessedData& output_data)
{
    if (frame.data == nullptr || frame.length == 0) {
        LOG_E("Invalid input frame data");
        return false;
    }

    uint8_t* data_ptr = static_cast<uint8_t*>(frame.data);

    // MJPEG判定 (SOIマーカー: 0xFF, 0xD8)
    bool is_mjpeg = (frame.length > 2 && data_ptr[0] == 0xFF && data_ptr[1] == 0xD8);

    if (is_mjpeg) {
        // --- 【修正】 EOI検索ロジック (全探索 & ログ出力) ---
        size_t actual_length = 0;
        bool eoi_found = false;

        // 末尾から先頭に向かって全探索する
        // (前回は1024バイト制限があったが、念のため全探索に変更)
        for (size_t i = frame.length - 1; i > 1; --i) {
            if (data_ptr[i] == 0xD9 && data_ptr[i-1] == 0xFF) {
                actual_length = i + 1;
                eoi_found = true;
                break;
            }
        }

        if (!eoi_found) {
            // ここでログが出れば、データが本当に壊れているか、マーカーがない
            LOG_W("Dropped Frame: No JPEG EOI marker found. Length: %zu", frame.length);
            return false;
        }
        // ----------------------------------------

        output_data.send_encoded_image.assign(data_ptr, data_ptr + actual_length);
        output_data.is_mjpeg_passthrough = true;
        
        int w, h, subsamp, colorspace; 
        if (tjDecompressHeader3(
                tj_dec_,
                data_ptr,
                actual_length, 
                &w,
                &h,
                &subsamp,
                &colorspace) != 0) {
            // ここで失敗する場合もログを出す
            LOG_W("Dropped Frame: Header decode failed: %s", tjGetErrorStr());
            return false;
        }

        size_t required_size = static_cast<size_t>(w * h * 3);
        if (bgr_buffer_.size() < required_size) {
            bgr_buffer_.resize(required_size);
        }

        if (tjDecompress2(
                tj_dec_,
                data_ptr,
                actual_length,
                bgr_buffer_.data(),
                w,
                0,
                h,
                TJPF_BGR,
                TJFLAG_FASTDCT) != 0) {
             LOG_W("Dropped Frame: Decompress failed: %s", tjGetErrorStr());
            return false;
        }

        output_data.raw_mat = cv::Mat(h, w, CV_8UC3, bgr_buffer_.data());

        return true;
    } else {
        // --- YUYV (Raw) ---
        int w = (frame.width > 0) ? frame.width : 640;
        int h = (frame.height > 0) ? frame.height : 480;

        size_t expected_size = static_cast<size_t>(w * h * 2);
        
        if (frame.length < expected_size) {
            return false;
        }

        try {
            cv::Mat yuyv_mat(h, w, CV_8UC2, frame.data);
            cv::cvtColor(yuyv_mat, output_data.raw_mat, cv::COLOR_YUV2BGR_YUYV);
        } catch (const cv::Exception& ex) {
            LOG_E("YUYV conversion failed: %s", ex.what());
            return false;
        }

        output_data.is_mjpeg_passthrough = false;

        return encode_image_to_jpeg(
            output_data.raw_mat,
            output_data.send_encoded_image,
            quality_);
    }
}

// AI解析用処理パイプライン
bool ImageProcessor::process_for_ai(const cv::Mat& src_img, AnalysisProcessedData& output_data)
{
    if (src_img.empty()) {
        return false;
    }

    try {
        cv::Mat denoised;
        cv::bilateralFilter(src_img, denoised, 5, 50, 50);

        cv::Mat gray, edges;
        cv::cvtColor(denoised, gray, cv::COLOR_BGR2GRAY);
        cv::Canny(gray, edges, 50, 150);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        output_data.std_processed_mat = src_img.clone();

        for (const auto& c : contours) {
            if (cv::contourArea(c) < 300.0) continue;

            cv::RotatedRect r = cv::minAreaRect(c);
            float w = r.size.width;
            float h = r.size.height;
            float aspect = std::max(w, h) / std::min(w, h);

            if (aspect < 3.0) continue;

            cv::Point2f pts[4];
            r.points(pts);
            for (int i = 0; i < 4; i++) {
                cv::line(output_data.std_processed_mat, pts[i], pts[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
            }
        }

        if (!encode_image_to_jpeg(
                output_data.std_processed_mat,
                output_data.send_encoder_image,
                quality_)) {
            LOG_E("Failed to encode analysis image");
            return false;
        }

    } catch (const cv::Exception& ex) {
        LOG_E("AI processing exception: %s", ex.what());
        return false;
    }

    return true;
}

bool ImageProcessor::encode_image_to_jpeg(
    const cv::Mat& img,
    std::vector<uint8_t>& encoded_buffer,
    uint8_t quality)
{
    if (img.empty()) return false;
    if (tj_enc_ == nullptr) return false;

    unsigned char* jpeg_ptr = nullptr;
    unsigned long jpeg_size = 0;

    int ret = tjCompress2(
        tj_enc_,
        img.data,
        img.cols,
        img.step,
        img.rows,
        TJPF_BGR,
        &jpeg_ptr,
        &jpeg_size,
        TJSAMP_420,
        quality,
        TJFLAG_FASTDCT
    );

    if (ret != 0) {
        LOG_E("TurboJPEG compress failed: %s", tjGetErrorStr());
        return false;
    }

    encoded_buffer.assign(jpeg_ptr, jpeg_ptr + jpeg_size);
    tjFree(jpeg_ptr);

    return true;
}
