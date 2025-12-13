/**
 *  @file	logger.hpp
 *  @brief	ログAPI
 *  @author	sawada souta
 *  @date	2025/12/13
 */

#ifndef LOGGER_HPP_
#define LOGGER_HPP_

#include <sys/types.h>
#include <unistd.h>

/**
 *  @brief	最初の呼び出し時のみgetpid()を実行
 *  @return	pid 
 *  @note	fork()を使う際は再呼び出し必須
 */
static inline pid_t log_getpid(void);

/** @brief log level critical */
#define LOG_C(fmt, ...)	dprintf(STDERR_FILENO, "[CRIT] [%d] " fmt "\n", log_getpid(), ##__VA_ARGS__)
/** @brief log level error */
#define LOG_E(fmt, ...)	dprintf(STDERR_FILENO, "[ERR] [%d] " fmt "\n", log_getpid(), ##__VA_ARGS__)
/** @brief log level warning */
#define LOG_W(fmt, ...)	dprintf(STDERR_FILENO, "[WARN] [%d] " fmt "\n", log_getpid(), ##__VA_ARGS__)
/** @brief log level info */
#define LOG_I(fmt, ...)	dprintf(STDERR_FILENO, "[INFO] [%d] " fmt "\n", log_getpid(), ##__VA_ARGS__)

/** @brief DEBUGマクロが有効なときだけLOG_Dマクロを有効にする */
#ifdef DEBUG
/** @brief log level debug */
#define LOG_D(fmt, ...)	dprintf(STDERR_FILENO, "[DEBUG] [%d] " fmt "\n", log_getpid(), ##__VA_ARGS__)
#else
/** @brief 何も実行しない */
#define LOG_D		((void)0)
#endif

static inline pid_t log_getpid(void)
{
	static pid_t pid = -1;

	if(pid == -1) {
		pid = getpid();
	}

	return pid;
}

#endif

