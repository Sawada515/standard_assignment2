/**
 * @file    image_processor.hpp
 * @brief   画像処理クラスヘッダ（YUYV変換 -> YOLO推論 -> 抵抗値推定 -> GUI出力）
 * @author  sawada souta
 * @date    2025-12-18
 */

#ifndef IMAGE_PROCESSOR_HPP_
#define IMAGE_PROCESSOR_HPP_

#include <cstdint>
#include <vector>
#include <string>

#include <turbojpeg.h>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

/**
 * @class ImageProcessor
 * @brief カメラ画像の変換、AI推論、GUI用画像生成を行うクラス
 * @note  カメラデバイス制御やネットワーク通信機能は持たない。純粋なデータ加工を担当する。
 */
class ImageProcessor {
public:
    /**
     * @struct ResistorInfo
     * @brief  検出された抵抗単体の情報
     */
    struct ResistorInfo {
        cv::Rect box;            /**< 画像上の検出矩形 (x, y, width, height) */
        float confidence;        /**< 検出の確信度 (0.0 - 1.0) */
        double resistance_value; /**< 推定された抵抗値 [Ω]。未計算/計算不能時は -1.0 */
    };

    /**
     * @struct GuiProcessedData
     * @brief  GUI（操縦者）へ送信するための画像データ
     */
    struct GuiProcessedData {
        std::vector<uint8_t> image; /**< JPEG圧縮された画像データ (描画処理済み) */
        uint32_t width  = 0;        /**< 画像の横幅 [px] */
        uint32_t height = 0;        /**< 画像の高さ [px] */
        bool is_jpeg = false;       /**< データ形式がJPEGか否か */
    };

    /**
     * @struct AiProcessedData
     * @brief  AI処理結果および解析用データ
     */
    struct AiProcessedData {
        std::vector<uint8_t> image; /**< BGR生画像データ (必要に応じて利用) */
        uint32_t width  = 0;        /**< 画像の横幅 [px] */
        uint32_t height = 0;        /**< 画像の高さ [px] */
        uint32_t channels = 3;      /**< チャンネル数 (通常3: BGR) */
        
        std::vector<ResistorInfo> resistors; /**< 検出された抵抗のリスト */
    };

    /**
     * @brief コンストラクタ
     * @param model_path   ONNXモデルファイルのパス
     * @param jpeg_quality GUI送信時のJPEG圧縮品質 (1-100)
     * @param resize_width 画像処理時の基準横幅（YUYV変換時のバッファ確保等に使用）
     */
    ImageProcessor(const std::string& model_path, int jpeg_quality, uint32_t resize_width);

    /**
     * @brief デストラクタ
     */
    ~ImageProcessor();

    /**
     * @brief  1フレーム分の画像処理を実行するメイン関数
     * @details
     * 1. YUYV形式からBGR形式への変換
     * 2. YOLOによる抵抗の物体検出
     * 3. 検出された領域の抵抗値推定（カラーコード読み取り）
     * 4. 結果の描画（バウンディングボックス、テキスト）
     * 5. GUI送信用へのJPEG圧縮
     * * @param[in]  yuyv      カメラからの生データ (YUYV形式)
     * @param[in]  width     画像の横幅
     * @param[in]  height    画像の高さ
     * @param[out] gui_data  GUI送信用の処理結果格納先
     * @param[out] ai_data   AI解析結果の格納先
     * @return true  処理成功
     * @return false 入力不正、または圧縮失敗等
     */
    bool process_frame(const uint8_t* yuyv,
                       uint32_t width,
                       uint32_t height,
                       GuiProcessedData& gui_data,
                       AiProcessedData& ai_data);

private:
    /**
     * @brief AI推論を実行し、抵抗の位置を検出する
     * @param[in]  input_image   入力画像 (BGR)
     * @param[out] out_resistors 検出結果のリスト (クリアしてから追加される)
     */
    void detect_resistors(const cv::Mat& input_image, std::vector<ResistorInfo>& out_resistors);

    /**
     * @brief 切り出された抵抗画像から抵抗値を推定する
     * @note  現在はガワのみの実装（ダミー値を返す）
     * @param[in] full_image 元の全体画像
     * @param[in] box        抵抗のバウンディングボックス
     * @return double 推定された抵抗値 [Ω]。推定不可の場合は -1.0
     */
    double estimate_resistance_value(const cv::Mat& full_image, const cv::Rect& box);

    /**
     * @brief 検出結果（枠線や数値）を画像に描画する
     * @param[in,out] image      描画対象の画像 (BGR)
     * @param[in]     resistors  描画する抵抗情報のリスト
     */
    void draw_results(cv::Mat& image, const std::vector<ResistorInfo>& resistors);

    /**
     * @brief BGR画像をTurboJPEGを使用して高速にJPEG圧縮する
     * @param[in]  bgr_mat 入力画像 (OpenCV Mat)
     * @param[in]  quality 圧縮品質 (1-100)
     * @param[out] jpeg    圧縮データの出力先ベクタ
     * @return true 成功 / false 失敗
     */
    bool bgr_to_jpeg(const cv::Mat& bgr_mat, int quality, std::vector<uint8_t>& jpeg);

    cv::Mat get_roi_resistor_image(const cv::Mat& base_image, const cv::Rect& box);

    int jpeg_quality_;          /**< JPEG圧縮品質 */
    uint32_t resize_width_;     /**< リサイズ幅 (現在未使用だが拡張用に保持) */

    // AIモデル関連
    cv::dnn::Net net_;          /**< OpenCV DNN ネットワークインスタンス */
    
    // 定数（本来はConfig等から読み込むのが望ましいが、簡易実装のためconstメンバとする）
    const float CONF_THRESHOLD = 0.45f; /**< 検出信頼度の閾値 */
    const float NMS_THRESHOLD  = 0.50f; /**< NMS（重なり除去）の閾値 */
    const int INPUT_SIZE = 640;         /**< YOLOv8モデルの入力サイズ (640x640) */

    cv::Mat blob_;
    tjhandle tj_instance_;
};

#endif // IMAGE_PROCESSOR_HPP_
