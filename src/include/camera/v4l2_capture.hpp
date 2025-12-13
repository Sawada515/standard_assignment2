/**
 * @file	v4l2_capture.hpp
 * @brief	Webカメラから画像データの取得
 * @author	sawada souta
 * @version 0.1
 * @date	2025-12-13
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
		typedef struct frame {
			void *data;			/**< @brief mmapの戻り値用 */
			uint32_t width;		/**< @brief 画像データの横幅 */
			uint32_t height;	/**< @brief 画像データの縦幅 */
		} Frame;

		/**
		 * @brief	V4L2Captureコンストラクタ
		 */
		V4L2Capture(void);

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
		bool read_frame(Frame& frame);

		/**
		 * @brief	キャプチャ停止
		 */
		void stop_stream(void);

		/**
		 * @brief	/dev/Video[0-9]のキャラクタデバイスを閉じる
		 */
		void close_device(void);

	private:
		std::string device_name_;	/**< @brief /dev/Video[0-9]のキャラクタデバイス名 */

		int device_fd_;				/**< @brief /dev/Video[0-9]のキャラクタデバイスファイルディスクリプタ */
};

#endif
