/* Exercises cancellation in the production client while a peer stalls. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ssp_platform.h"
#include "include/ssp/ssp.h"
#ifndef _WIN32
#include <pthread.h>
#endif

struct connection {
	ssp_client_t *client;
	atomic_bool done;
};

#ifdef _WIN32
static DWORD WINAPI connect_thread(void *arg)
#else
static void *connect_thread(void *arg)
#endif
{
	struct connection *connection = arg;
	ssp_client_connect(connection->client);
	atomic_store(&connection->done, true);
	return 0;
}

static void pause_ms(void)
{
#ifdef _WIN32
	Sleep(10);
#else
	struct timespec delay = {0, 10000000};
	nanosleep(&delay, NULL);
#endif
}

static int test_stop(int partial_length)
{
	ssp_socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address = {0};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (listener == SSP_INVALID_SOCKET || bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
	    listen(listener, 1))
		return 1;
#ifdef _WIN32
	int size = sizeof(address);
#else
	socklen_t size = sizeof(address);
#endif
	if (getsockname(listener, (struct sockaddr *)&address, &size))
		return 1;
	struct connection connection = {0};
	atomic_init(&connection.done, false);
	connection.client = ssp_client_create();
	if (!connection.client)
		return 1;
	ssp_client_set_target(connection.client, "127.0.0.1", ntohs(address.sin_port));
#ifdef _WIN32
	HANDLE thread = CreateThread(NULL, 0, connect_thread, &connection, 0, NULL);
	if (!thread)
		return 1;
#else
	pthread_t thread;
	if (pthread_create(&thread, NULL, connect_thread, &connection))
		return 1;
#endif
	ssp_pollfd_t pfd = {0};
	pfd.fd = listener;
	pfd.events = SSP_POLLIN;
	if (ssp_poll(&pfd, 1, 2000) <= 0)
		exit(1);
	ssp_socket_t peer = accept(listener, NULL, NULL);
	if (peer == SSP_INVALID_SOCKET)
		exit(1);
	if (partial_length) {
		/* Either half a length prefix or a complete prefix with a partial INIT. */
		char prefix[] = {0, 0, 0, 63, 0x64};
		int length = partial_length == 1 ? 2 : sizeof(prefix);
		if (send(peer, prefix, length, SSP_MSG_NOSIGNAL) != length)
			exit(1);
	}
	for (int i = 0; i < 20; ++i)
		pause_ms();
	ssp_client_stop(connection.client);
	int64_t deadline = ssp_clock_ms() + 2000;
	while (!atomic_load(&connection.done) && ssp_clock_ms() < deadline)
		pause_ms();
	if (!atomic_load(&connection.done)) {
		fprintf(stderr, "stop failed to unblock stalled peer (%d)\n", partial_length);
		exit(1); /* Do not destroy a client still used by its connection thread. */
	}
#ifdef _WIN32
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
#else
	pthread_join(thread, NULL);
#endif
	ssp_client_destroy(connection.client);
	ssp_close(peer);
	ssp_close(listener);
	return 0;
}

int main(void)
{
	if (ssp_platform_init())
		return 1;
	int result = test_stop(0) || test_stop(1) || test_stop(2);
	ssp_platform_cleanup();
	if (!result)
		puts("PASS: stop during idle handshake, partial prefix, and partial payload");
	return result;
}
