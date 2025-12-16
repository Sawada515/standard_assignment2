/**
 * @file    udp_sender.cpp
 * @brief   UDPパケット送信クラスの実装
 * @author  sawada souta
 * @date    2025-12-14
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include <sys/uio.h>
#include <algorithm>
#include <thread>
#include <chrono>

#include "network/udp_sender.hpp"
#include "logger/logger.hpp"

UDPSender::UDPSender(const std::string& ip, uint16_t port)
    : sock_fd_(-1), is_valid_(false)
{
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ < 0) {
        LOG_E("Failed to create UDP socket: %s", std::strerror(errno));
        
        return;
    }

    std::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr) <= 0) {
        LOG_E("Invalid IP address: %s", ip.c_str());

        close(sock_fd_);
        sock_fd_ = -1;

        return;
    }

    is_valid_ = true;
    
    LOG_I("UDPSender initialized. Target: %s:%d", ip.c_str(), port);
}

UDPSender::~UDPSender()
{
    if (sock_fd_ >= 0) {
        close(sock_fd_);

        LOG_I("UDP socket closed");
    }
}

bool UDPSender::send(const void* data, size_t size)
{
    if (!is_valid_ || sock_fd_ < 0) {
        LOG_E("Socket is not valid");
        return false;
    }

    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t offset = 0;

    const size_t MAX_CHUNK_SIZE = 1400;

    while (offset < size) {
        // 今回送るデータサイズ
        size_t chunk_size = std::min(MAX_CHUNK_SIZE, size - offset);
        
        // 終了フラグ (0: 続きあり, 1: これで最後)
        uint8_t flag = (offset + chunk_size == size) ? 1 : 0;

        // --- ここが Zero-copy の肝 (iovec) ---
        struct iovec iov[2];
        
        // 1つ目のデータ: ヘッダー (1byte)
        iov[0].iov_base = &flag;
        iov[0].iov_len = 1;

        // 2つ目のデータ: 画像データのポインタ (コピーせず参照するだけ)
        iov[1].iov_base = const_cast<uint8_t*>(ptr + offset);
        iov[1].iov_len = chunk_size;

        // メッセージヘッダの作成
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &addr_;             // 送信先アドレス
        msg.msg_namelen = sizeof(addr_);
        msg.msg_iov = iov;                 // データの配列
        msg.msg_iovlen = 2;                // 配列の長さ (ヘッダー + 本体)

        // 送信実行 (カーネル内で結合される)
        ssize_t sent_bytes = sendmsg(sock_fd_, &msg, 0);

        if (sent_bytes < 0) {
            LOG_E("UDP sendmsg failed: %s", std::strerror(errno));

            return false;
        }

        offset += chunk_size;
    }

    return true;
}
