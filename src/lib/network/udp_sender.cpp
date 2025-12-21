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

    int sendbuf_size = 4 * 1024 * 1024;

    setsockopt(sock_fd_, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, sizeof(sendbuf_size));

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

    int packet_count = 0;

    while (offset < size) {
        size_t chunk_size = std::min(MAX_CHUNK_SIZE, size - offset);

        uint8_t flag = (offset + chunk_size == size) ? 1 : 0;

        struct iovec iov[2];

        iov[0].iov_base = &flag;
        iov[0].iov_len = 1;

        iov[1].iov_base = const_cast<uint8_t*>(ptr + offset);
        iov[1].iov_len = chunk_size;

        // メッセージヘッダの作成
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &addr_;             // 送信先アドレス
        msg.msg_namelen = sizeof(addr_);
        msg.msg_iov = iov;                 // データの配列
        msg.msg_iovlen = 2;                // 配列の長さ (ヘッダー + 本体)

        ssize_t send_bytes;
        int retry_count = 0;
        const int MAX_RETRIES = 5;

        do {
            send_bytes = sendmsg(sock_fd_, &msg, 0); //送信実行 カーネル側

            if (send_bytes >= 0) {
                break;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) {
                retry_count += 1;

                if (retry_count >= MAX_RETRIES) {
                    LOG_E("UDP send buffer full, dropped packet.");

                    return false;   // ここで終わるとGUI側で線が入ったりする
                }

                std::this_thread::sleep_for(std::chrono::microseconds(500));
            } else {
                LOG_E("UDP sendmsg fatal error : %s", std::strerror(errno));

                return false;
            }
        } while (retry_count <= MAX_RETRIES);

        offset += chunk_size;

        packet_count += 1;

        if (packet_count % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    return true;
}
