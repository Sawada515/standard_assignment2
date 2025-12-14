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

    ssize_t sent_bytes = sendto(sock_fd_, data, size, 0,
                                (struct sockaddr*)&addr_, sizeof(addr_));

    if (sent_bytes < 0) {
        LOG_E("UDP send failed: %s", std::strerror(errno));
        
        return false;
    } else if (static_cast<size_t>(sent_bytes) != size) {
        LOG_W("Incomplete UDP send: %zd / %zu bytes", sent_bytes, size);

        return false;
    }

    return true;
}
