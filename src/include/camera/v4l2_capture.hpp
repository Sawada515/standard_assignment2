/**
 * @file    v4l2_capture.hpp
 * @brief   Webカメラから画像データの取得
 * @author  sawada souta
 * @version 0.4
 * @date    2025-12-17
 * @note    YUYVフォーマットの画像データを1枚取得
 */

#ifndef V4L2_CAPTURE_HPP_
#define V4L2_CAPTURE_HPP_

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief V4L2 user space APIを使ってWebカメラから画像データを取得するクラス
 */
class V4L2Capture {
public:
    /**
     * @brief   画像データへのポインタ 画像サイズなどのフレーム情報
     */
    struct Frame {
        std::vector<uint8_t>data;   /**< @brief 画像データへのポインタ */
        
        uint32_t width = 0;         /**< @brief 画像データの横幅 */
        uint32_t height = 0;        /**< @brief 画像データの縦幅 */

        uint32_t fourcc = 0;        /**< @brief 画像データのフォーマット fourcc */
    };

    /**
     * @brief       V4L2Captureコンストラクタ
     * @param[in]   device_name /dev/Video[0-9]のキャラクタデバイス名
     * @param[in]   width 取得する画像データの横幅
     * @param[in]   height 取得する画像データの縦幅
     */
    V4L2Capture(const std::string& device_name, uint32_t width, uint32_t height);

    /**
     * @brief   V4L2Captureデコンストラクタ
     */
    ~V4L2Capture(void);

    /**
     * @brief       デバイスを開き、メモリマップまでを完了させる（追加）
     * @return      true エラーなし
     * @return      false エラーあり
     * @note        ループ処理に入る前に必ず実行すること
     */
    bool initialize(void);

    /**
     * @brief       キャプチャしたフレームデータを取得
     * @param[in]   frame フレーム情報を渡す
     * @return      true エラーなし
     * @return      false エラーあり
     * @note        USB帯域制御のため、内部でSTREAMON/STREAMOFFを行う
     */
    bool get_once_frame(V4L2Capture::Frame& frame);

private:
    /**
     * @brief       /dev/Video[0-9]のキャラクタデバイスを開く
     * @param[in]   device /dev/Video[0-9]を指定
     * @return      true エラーなし
     * @return      false エラーあり
     * @note        エラーの場合はログを参照
     */
    bool open_device(void);

    /**
     * @brief   /dev/Video[0-9]のキャラクタデバイスを閉じる
     */
    void close_device(void);

    /**
     * @brief       フレームフォーマットを設定する
     * @param[in]   width 取得する画像データの横幅
     * @param[in]   height 取得する画像データの縦幅
     * @param[in]   fourcc 取得する画像データのフォーマット fourcc
     * @return      true エラーなし
     * @return      false エラーあり
     * @note        エラーの場合はログを参照
     */
    bool set_frame_format(uint32_t width, uint32_t height, uint32_t fourcc);

    std::string device_name_;   /**< @brief /dev/Video[0-9]のキャラクタデバイス名 */

    int device_fd_;             /**< @brief /dev/Video[0-9]のキャラクタデバイスファイルディスクリプタ */

    uint32_t width_;            /**< @brief 取得する画像データの横幅 */
    uint32_t height_;           /**< @brief 取得する画像データの縦幅 */

    /**
     * @brief mmap情報を保持するための内部構造体（追加）
     */
    struct Buffer {
        void* start;
        size_t length;
    };
    std::vector<Buffer> buffers_; /**< @brief マップされたバッファのリスト */
};

#endif
