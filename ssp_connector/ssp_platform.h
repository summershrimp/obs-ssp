/*
 * ssp_platform.h — Platform abstraction for POSIX / Windows sockets
 *
 * Copyright (c) 2026, Hedonistic, LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SSP_PLATFORM_H
#define SSP_PLATFORM_H

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

typedef SOCKET ssp_socket_t;
#define SSP_INVALID_SOCKET INVALID_SOCKET
#define SSP_SOCKET_ERROR   SOCKET_ERROR

static inline int ssp_platform_init(void)
{
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void ssp_platform_cleanup(void) { WSACleanup(); }
static inline int  ssp_close(ssp_socket_t s)  { return closesocket(s); }
static inline int  ssp_errno(void)             { return WSAGetLastError(); }
static inline bool ssp_errno_intr(void)        { return false; } /* no EINTR on Windows */

/* Windows send() doesn't support MSG_NOSIGNAL */
#define SSP_MSG_NOSIGNAL 0

/* poll() → WSAPoll() */
#define SSP_POLLIN   POLLIN
#define SSP_POLLERR  POLLERR
#define SSP_POLLHUP  POLLHUP
#define ssp_poll(fds, n, ms)  WSAPoll(fds, n, ms)
typedef WSAPOLLFD ssp_pollfd_t;

/* Monotonic clock */
#include <windows.h>
static inline int64_t ssp_clock_ms(void)
{
	LARGE_INTEGER freq, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	return (int64_t)(now.QuadPart * 1000 / freq.QuadPart);
}

#else /* POSIX */

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>

typedef int ssp_socket_t;
#define SSP_INVALID_SOCKET (-1)
#define SSP_SOCKET_ERROR   (-1)

static inline int  ssp_platform_init(void)    { signal(SIGPIPE, SIG_IGN); return 0; }
static inline void ssp_platform_cleanup(void) { }
static inline int  ssp_close(ssp_socket_t s)  { return close(s); }
static inline int  ssp_errno(void)             { return errno; }
static inline bool ssp_errno_intr(void)        { return errno == EINTR; }

#define SSP_MSG_NOSIGNAL MSG_NOSIGNAL

#define SSP_POLLIN   POLLIN
#define SSP_POLLERR  POLLERR
#define SSP_POLLHUP  POLLHUP
#define ssp_poll(fds, n, ms) poll(fds, n, ms)
typedef struct pollfd ssp_pollfd_t;

static inline int64_t ssp_clock_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif /* _WIN32 */
#endif /* SSP_PLATFORM_H */
