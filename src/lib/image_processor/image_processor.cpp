/**
 * @file    image_processor.cpp
 * @brief   画像処理クラスの実装（YUYV変換, YOLO推論, TurboJPEG圧縮）
 * @author  sawada souta
 * @date    2025-12-18
 */

#include "image_processor/image_processor.hpp"
#include "logger/logger.hpp"

#include <iostream>
#include <vector>
#include <algorithm>
#include <turbojpeg.h>
#include <opencv2/imgproc.hpp>

// コンストラクタ
ImageProcessor::ImageProcessor(const std::string& model_path, int jpeg_quality, uint32_t resize_width) :
    jpeg_quality_(jpeg_quality),
    resize_width_(resize_width)
{
    try {
        LOG_I("[ImageProcessor] Loading AI Model from: ");
        
        net_ = cv::dnn::readNetFromONNX(model_path);
        
        // ラズパイ(CPU)向けに最適化されたバックエンド設定
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        
        LOG_I("[ImageProcessor] Model loaded successfully." );
    } catch (const cv::Exception& e) {
        LOG_E("[ImageProcessor] Error loading model: ");

        exit(-1);
    }

    tj_instance_ = tjInitCompress();
}

// デストラクタ
ImageProcessor::~ImageProcessor()
{
    tjDestroy(tj_instance_);
}


// Public: フレーム処理メイン
bool ImageProcessor::process_frame(const uint8_t* yuyv,
                                   uint32_t width,
                                   uint32_t height,
                                   GuiProcessedData& gui_data,
                                   AiProcessedData& ai_data)
{
    // 入力チェック
    if (!yuyv || width == 0 || height == 0) {
        return false;
    }

    /* ---------- 1. YUYV -> BGR 変換 (OpenCV) ---------- */
    // YUYVデータ(2ch)としてMatを作成（コピーなし）
    cv::Mat src_mat(height, width, CV_8UC2, (void*)yuyv);
    
    // 出力バッファのサイズ確保とMatラップ
    size_t bgr_size = width * height * 3;
    if (ai_data.image.size() != bgr_size) {
        ai_data.image.resize(bgr_size);
    }
    // ai_data.imageのメモリ領域を直接使うMatを作成
    cv::Mat dst_mat(height, width, CV_8UC3, ai_data.image.data());
    
    // 色空間変換 (NEON最適化が効く)
    cv::cvtColor(src_mat, dst_mat, cv::COLOR_YUV2BGR_YUYV);

    // AIデータヘッダ情報更新
    ai_data.width = width;
    ai_data.height = height;
    ai_data.channels = 3;

    /* ---------- 2. 抵抗検出 (YOLO) ---------- */
    // 結果は ai_data.resistors に格納される
    detect_resistors(dst_mat, ai_data.resistors);

    /* ---------- 3. 抵抗値推定 & 結果格納 ---------- */
    for (auto& resistor : ai_data.resistors) {
        // 画像処理でカラーコードを読む（現在はダミー実装）
        resistor.resistance_value = estimate_resistance_value(dst_mat, resistor.box);
    }

    /* ---------- 4. GUI用に結果を描画 ---------- */
    // 受信側で確認しやすいよう、画像自体に枠線や数値を書き込む
    draw_results(dst_mat, ai_data.resistors);

    /* ---------- 5. JPEG圧縮 (TurboJPEG) ---------- */
    // 描画済みの画像を圧縮してGUIデータとする
    if (!bgr_to_jpeg(dst_mat, jpeg_quality_, gui_data.image)) {
        return false;
    }
    
    gui_data.width = width;
    gui_data.height = height;
    gui_data.is_jpeg = true;

    return true;
}


void ImageProcessor::detect_resistors(const cv::Mat& input_image, std::vector<ResistorInfo>& out_resistors)
{
    out_resistors.clear();
    
    // モデルがロードできていなければ何もしない
    if (net_.empty()) return;

    // 前処理: 画像をYOLOの入力サイズにリサイズし、正規化(1/255)する

    if (blob_.empty()) {
        blob_.create(1, 3 * INPUT_SIZE * INPUT_SIZE, CV_32F);
    }

    if (input_image.empty()) {
        LOG_E("input image is empty");

        return;
    }

    LOG_I("DNN input : %dx%d ch = %d", input_image.cols, input_image.rows, \
                                        input_image.channels());

    cv::dnn::blobFromImage(input_image, blob_, 1.0/255.0, cv::Size(INPUT_SIZE, INPUT_SIZE), cv::Scalar(), true, false);
    net_.setInput(blob_);

    // 推論実行
    std::vector<cv::Mat> outputs;
    net_.forward(outputs, net_.getUnconnectedOutLayersNames());

    // 後処理
    const cv::Mat& out = outputs[0];

    const int rows = out.size[2];
    const int dims = out.size[1];

    const float* data = reinterpret_cast<const float*>(out.data);

    // data layout
    //data[rows * 0 + i] -> cx
    //data[rows * 1 + i] -> cy
    //data[rows * 2 + i] -> w
    //data[rows * 3 + i] -> h
    //data[rows * 4 + i] -> confidence

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;

    boxes.reserve(128);
    confidences.reserve(128);

    const float x_factor = static_cast<float>(input_image.cols) / INPUT_SIZE;
    const float y_factor = static_cast<float>(input_image.rows) / INPUT_SIZE;

    for (int i = 0; i < rows; ++i) {
        float confidence = data[rows * 4 + i];

        if (confidence < CONF_THRESHOLD)
            continue;

        float cx = data[rows * 0 + i];
        float cy = data[rows * 1 + i];
        float w  = data[rows * 2 + i];
        float h  = data[rows * 3 + i];

        int left   = static_cast<int>((cx - 0.5f * w) * x_factor);
        int top    = static_cast<int>((cy - 0.5f * h) * y_factor);
        int width  = static_cast<int>(w * x_factor);
        int height = static_cast<int>(h * y_factor);

        boxes.emplace_back(left, top, width, height);
        confidences.emplace_back(confidence);
    }

    /* ---------- NMS ---------- */
    std::vector<int> indices;
    cv::dnn::NMSBoxes(
        boxes,
        confidences,
        CONF_THRESHOLD,
        NMS_THRESHOLD,
        indices
    );

    out_resistors.reserve(indices.size());

    for (int idx : indices) {
        ResistorInfo info;
        info.box = boxes[idx];
        info.confidence = confidences[idx];
        info.resistance_value = -1.0;
        out_resistors.push_back(info);
    }
}

double ImageProcessor::estimate_resistance_value(const cv::Mat& full_image, const cv::Rect& box)
{
    // 画像範囲からはみ出さないようクリップする
    cv::Rect safe_box = box & cv::Rect(0, 0, full_image.cols, full_image.rows);
    
    // 面積が0なら処理不可
    if (safe_box.area() == 0) return -1.0;

    // ROI (Region of Interest) 切り出し
    cv::Mat resistor_roi = full_image(safe_box);

    /* * TODO: 抵抗値読み取りロジックの実装
     * 1. ROI画像をHSVなどに変換
     * 2. エッジ検出や輪郭抽出で抵抗本体の向きを補正
     * 3. 中央ラインの色分布（カラーコード）を取得
     * 4. 色の並びから抵抗値を計算
     */

    // 現在は仮実装としてダミー値を返す
    return 1000.0; 
}

void ImageProcessor::draw_results(cv::Mat& image, const std::vector<ResistorInfo>& resistors)
{
    const cv::Scalar COLOR_GREEN(0, 255, 0); // BGR
    const cv::Scalar COLOR_BLACK(0, 0, 0);

    for (const auto& r : resistors) {
        // バウンディングボックス描画
        cv::rectangle(image, r.box, COLOR_GREEN, 2);

        // ラベルテキストの作成
        std::string label = "Resistor";
        if (r.resistance_value > 0) {
            // 小数点以下を切り捨てて表示
            label += ": " + std::to_string((int)r.resistance_value) + " ohm";
        }

        // ラベルの背景（黒枠）を描画して文字を見やすくする
        int baseLine;
        cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
        
        cv::Rect labelBackground(
            cv::Point(r.box.x, r.box.y - labelSize.height),
            cv::Size(labelSize.width, labelSize.height + baseLine)
        );
        cv::rectangle(image, labelBackground, COLOR_GREEN, cv::FILLED);

        // テキスト描画
        cv::putText(image, label, cv::Point(r.box.x, r.box.y),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, COLOR_BLACK, 1);
    }
}

bool ImageProcessor::bgr_to_jpeg(const cv::Mat& bgr_mat, int quality, std::vector<uint8_t>& jpeg)
{
    if (bgr_mat.empty()) return false;

    // TurboJPEG インスタンス初期化

    unsigned char* outbuf = nullptr; // TurboJPEGが内部で確保するバッファ
    unsigned long outsize = 0;

    // 圧縮実行
    int ret = tjCompress2(
        tj_instance_,
        bgr_mat.data,   // 入力バッファ
        bgr_mat.cols,   // 幅
        0,              // pitch (0=自動計算)
        bgr_mat.rows,   // 高さ
        TJPF_BGR,       // 入力ピクセルフォーマット
        &outbuf,        // 出力バッファアドレスへのポインタ
        &outsize,       // 出力サイズへのポインタ
        TJSAMP_444,     // サブサンプリング (444は高画質)
        quality,        // 画質 (1-100)
        TJFLAG_FASTDCT  // 高速DCTアルゴリズム使用
    );

    if (ret != 0) {
        // 圧縮失敗時、確保されたバッファがあれば解放
        if (outbuf) tjFree(outbuf);
        return false;
    }

    // std::vector にコピー（またはムーブしたいがAPI仕様上コピーが安全）
    try {
        jpeg.assign(outbuf, outbuf + outsize);
    } catch (...) {
        tjFree(outbuf);
        return false;
    }

    // TurboJPEG確保メモリの解放
    tjFree(outbuf);

    return true;
}
