/**
 * @file    udp_sender.hpp
 * @brief   UDPパケット送信クラス
 * @author  sawada souta
 * @date    2025-12-14
 */

#ifndef UDP_SENDER_HPP_
#define UDP_SENDER_HPP_

#include <string>
#include <cstdint>
#include <netinet/in.h> 

/**
 * @brief 指定したIPとポートにUDPデータを送信するクラス
 */
class UDPSender {
public:
    /**
     * @brief コンストラクタ（ソケットの作成とアドレス設定）
     * @param[in] ip   送信先IPアドレス (例: "192.168.1.10")
     * @param[in] port 送信先ポート番号 (例: 5000)
     */
    UDPSender(const std::string& ip, uint16_t port);

    /**
     * @brief デストラクタ（ソケットを閉じる）
     */
    ~UDPSender();

    /**
     * @brief データを送信する
     * @param[in] data 送信データへのポインタ
     * @param[in] size 送信データのサイズ (バイト)
     * @return true 送信成功
     * @return false 送信失敗
     */
    bool send(const void* data, size_t size);

private:
    int sock_fd_;               /**< ソケットファイルディスクリプタ */
    struct sockaddr_in addr_;   /**< 送信先アドレス情報 */
    bool is_valid_;             /**< 初期化成功フラグ */
};

#endif