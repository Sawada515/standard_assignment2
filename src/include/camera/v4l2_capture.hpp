/**
 * @file	v4l2_capture.hpp
 * @brief	Webカメラから画像データの取得
 * @author	sawada souta
 * @version 0.1
 * @date	2025-12-13
 * @note	MJPEG対応のWebカメラを前提
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
	 * @brief	画像データへのポインタ 画像サイズなどのフレーム情報
	 */
	struct Frame {
		uint8_t *data = nullptr;	/**< @brief 画像データへのポインタ */
		
		size_t length = 0;			/**< @brief 画像データのサイズ */
		
		uint32_t width = 0;			/**< @brief 画像データの横幅 */
		uint32_t height = 0;		/**< @brief 画像データの縦幅 */

		int v4l2_queue_index = -1;	/**< @brief V4L2キュー内のバッファインデックス */

		V4L2Capture *owner = nullptr;	/**< @brief フレームを所有するV4L2Captureオブジェクトへのポインタ */

		Frame() = default;

		Frame(const Frame&) = delete;	/**< @brief コピーコンストラクタ禁止 */
		Frame& operator=(const Frame&) = delete;	/**< @brief コピー代入禁止 */

		Frame(Frame&& other) noexcept	/**< @brief ムーブコンストラクタ */
		{
			*this = std::move(other);
		}
		
		Frame& operator=(Frame&& other) noexcept	/**< @brief ムーブ代入演算子 */
		{
			if (this != &other) {
				data = other.data;
				length = other.length;
				width = other.width;
				height = other.height;
				v4l2_queue_index = other.v4l2_queue_index;
				owner = other.owner;

				other.data = nullptr;
				other.length = 0;
				other.width = 0;
				other.height = 0;
				other.v4l2_queue_index = -1;
				other.owner = nullptr;
			}

			return *this;
		}

		~Frame() {
			if (owner && v4l2_queue_index >= 0) {
				owner->release_frame(*this);
			}
		}
	};

	/**
	 * @brief		V4L2Captureコンストラクタ
	 * @param[in]	width 取得する画像データの横幅
	 * @param[in]	height 取得する画像データの縦幅
	 */
	V4L2Capture(uint32_t width, uint32_t height);

	/**
	 * @brief	V4L2Captureデコンストラクタ
	 */
	~V4L2Capture(void);

	/**
	 * @brief		/dev/Video[0-9]のキャラクタデバイスを開く
	 * @param[in]	device /dev/Video[0-9]を指定
	 * @return		true エラーなし
	 * @return		false エラーあり
	 * @note		エラーの場合はログを参照
	 */
	bool open_device(const std::string& device);

	/**
	 * @brief	キャプチャ開始
	 * @return	true エラーなし
	 * @return	false エラーあり
	 * @note	エラーの場合はログを参照
	 */
	bool start_stream(void);

	/**
	 * @brief		キャプチャしたフレームデータを取得
	 * @param[in]	frame フレーム情報を渡す
	 * @return		true エラーなし
	 * @return		false エラーあり
	 * @note		エラーの場合はログを参照
	 */
	bool get_frame(V4L2Capture::Frame& frame);

	/**
	 * @brief		キャプチャしたフレームデータを解放
	 * @param[in]	frame フレーム情報を渡す
	 * @return		true エラーなし
	 * @return		false エラーあり
	 * @note		エラーの場合はログを参照
	 */
	bool release_frame(V4L2Capture::Frame& frame);

	/**
	 * @brief	キャプチャ停止
	 */
	void stop_stream(void);

	/**
	 * @brief	/dev/Video[0-9]のキャラクタデバイスを閉じる
	 */
	void close_device(void);

private:
	/**
	 * @brief	mmapしたバッファ情報
	 */
	struct Buffer {
		void *start;
		size_t length;
	};

	std::string device_name_;	/**< @brief /dev/Video[0-9]のキャラクタデバイス名 */

	int device_fd_;				/**< @brief /dev/Video[0-9]のキャラクタデバイスファイルディスクリプタ */

	std::vector<Buffer> buffers_;	/**< @brief mmapしたバッファ情報 */

	uint32_t width_;			/**< @brief 取得する画像データの横幅 */
	uint32_t height_;			/**< @brief 取得する画像データの縦幅 */
};

#endif
