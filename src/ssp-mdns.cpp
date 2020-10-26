/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <map>
#include <string>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <mdns.h>
#ifdef _WIN32
#include <iphlpapi.h>
#define sleep(x) Sleep(x * 1000)
#else
#include <netdb.h>
#include <ifaddrs.h>
#endif

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

#include "obs-ssp.h"
#include "ssp-mdns.h"

#define DEFAULT_TTL 60

struct mdns_args {
    bool running;
    const char *service_str;
    size_t service_str_size;
} g_mdns_args;

static uint32_t service_address_ipv4;
static uint8_t service_address_ipv6[16];

static int has_ipv4;
static int has_ipv6;

uint16_t current_transaction_id;
struct mdns_record current_mdns_record;

pthread_t mdns_thread;

#define MDNS_BUFFER_SIZE 2048

static char addrbuffer[64];
static char namebuffer[256];
static char sendbuffer[256];
static char entrybuffer[256];
static mdns_record_txt_t txtbuffer[128];

std::map<std::string, mdns_record> ssp_records;
std::mutex ssp_records_lock;

static mdns_string_t
ipv4_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in* addr, size_t addrlen) {
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    int ret = getnameinfo((const struct sockaddr*)addr, addrlen,
                          host, NI_MAXHOST, service, NI_MAXSERV,
                          NI_NUMERICSERV | NI_NUMERICHOST);
    int len = 0;
    if (ret == 0) {
        if (addr->sin_port != 0)
            len = snprintf(buffer, capacity, "%s:%s", host, service);
        else
            len = snprintf(buffer, capacity, "%s", host);
    }
    if (len >= (int)capacity)
        len = (int)capacity - 1;
    mdns_string_t str = {buffer, (size_t)len};
    return str;
}

static mdns_string_t
ipv6_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in6* addr, size_t addrlen) {
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    int ret = getnameinfo((const struct sockaddr*)addr, addrlen,
                          host, NI_MAXHOST, service, NI_MAXSERV,
                          NI_NUMERICSERV | NI_NUMERICHOST);
    int len = 0;
    if (ret == 0) {
        if (addr->sin6_port != 0)
            len = snprintf(buffer, capacity, "[%s]:%s", host, service);
        else
            len = snprintf(buffer, capacity, "%s", host);
    }
    if (len >= (int)capacity)
        len = (int)capacity - 1;
    mdns_string_t str = {buffer, (size_t)len};
    return str;
}

static mdns_string_t
ip_address_to_string(char* buffer, size_t capacity, const struct sockaddr* addr, size_t addrlen) {
    if (addr->sa_family == AF_INET6)
        return ipv6_address_to_string(buffer, capacity, (const struct sockaddr_in6*)addr, addrlen);
    return ipv4_address_to_string(buffer, capacity, (const struct sockaddr_in*)addr, addrlen);
}

static int
query_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
               uint16_t transaction_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void* data,
               size_t size, size_t name_offset, size_t name_length, size_t record_offset,
               size_t record_length, void* user_data) {
    if(transaction_id != current_transaction_id) {
        current_mdns_record.has_ptr = false;
        current_mdns_record.has_a = false;
        current_mdns_record.has_aaaa = false;
        current_transaction_id = transaction_id;
    }
    ttl = DEFAULT_TTL; // The mdns library we use cannot query all interface so we use a longer ttl.
    if (rtype == MDNS_RECORDTYPE_PTR) {
        mdns_string_t ptr_str = mdns_record_parse_ptr(data, size, record_offset, record_length,
                                                      namebuffer, sizeof(namebuffer));
        std::string ptr(ptr_str.str, ptr_str.length - strlen(ZCAM_QUERY_DOMAIN) - 2);
        current_mdns_record.ptr_record = ptr;
        current_mdns_record.last_available = os_gettime_ns() / 1000000 + ttl * 1000;
        current_mdns_record.has_ptr = true;
    }
    else if (current_mdns_record.has_ptr && rtype == MDNS_RECORDTYPE_A && from->sa_family == AF_INET) {
        struct sockaddr_in addr;
        mdns_record_parse_a(data, size, record_offset, record_length, &addr);
        memcpy(&(current_mdns_record.a_record), &addr, sizeof(current_mdns_record.a_record));
        current_mdns_record.has_a = true;
        current_mdns_record.last_available = os_gettime_ns() / 1000000 + ttl * 1000;
        ssp_records_lock.lock();
        ssp_records[current_mdns_record.ptr_record] = current_mdns_record;
        ssp_records_lock.unlock();
    }
    else if (current_mdns_record.has_ptr && rtype == MDNS_RECORDTYPE_AAAA && from->sa_family == AF_INET6) {
        struct sockaddr_in6 addr;
        mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
        memcpy(&(current_mdns_record.aaaa_record), &addr, sizeof(current_mdns_record.aaaa_record));
        current_mdns_record.has_aaaa = true;
        current_mdns_record.last_available = os_gettime_ns() / 1000000 + ttl * 1000;
        ssp_records_lock.lock();
        ssp_records[current_mdns_record.ptr_record] = current_mdns_record;
        ssp_records_lock.unlock();
    }
    return 0;
}


static int
open_client_sockets(int* sockets, int max_sockets, int port) {
	// When sending, each socket can only send to one network interface
	// Thus we need to open one socket for each interface and address family
	int num_sockets = 0;

#ifdef _WIN32

	IP_ADAPTER_ADDRESSES* adapter_address = 0;
	ULONG address_size = 8000;
	unsigned int ret;
	unsigned int num_retries = 4;
	do {
		adapter_address = (IP_ADAPTER_ADDRESSES*)malloc(address_size);
		ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0,
		                           adapter_address, &address_size);
		if (ret == ERROR_BUFFER_OVERFLOW) {
			free(adapter_address);
			adapter_address = 0;
		} else {
			break;
		}
	} while (num_retries-- > 0);

	if (!adapter_address || (ret != NO_ERROR)) {
		free(adapter_address);
		ssp_blog(LOG_ERROR, "Failed to get network adapter addresses");
		return num_sockets;
	}

	int first_ipv4 = 1;
	int first_ipv6 = 1;
	for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
		if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
			continue;
		if (adapter->OperStatus != IfOperStatusUp)
			continue;

		for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
		     unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
				struct sockaddr_in* saddr = (struct sockaddr_in*)unicast->Address.lpSockaddr;
				if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) ||
				    (saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
				    (saddr->sin_addr.S_un.S_un_b.s_b3 != 0) ||
				    (saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
					int log_addr = 0;
					if (first_ipv4) {
						service_address_ipv4 = saddr->sin_addr.S_un.S_addr;
						first_ipv4 = 0;
						log_addr = 1;
					}
					has_ipv4 = 1;
					if (num_sockets < max_sockets) {
						saddr->sin_port = htons((unsigned short)port);
						int sock = mdns_socket_open_ipv4(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
							log_addr = 1;
						} else {
							log_addr = 0;
						}
					}
					if (log_addr) {
						char buffer[128];
						mdns_string_t addr = ipv4_address_to_string(buffer, sizeof(buffer), saddr,
						                                            sizeof(struct sockaddr_in));
					}
				}
			} else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
				struct sockaddr_in6* saddr = (struct sockaddr_in6*)unicast->Address.lpSockaddr;
				static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
				                                          0, 0, 0, 0, 0, 0, 0, 1};
				static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
				                                                 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
				if ((unicast->DadState == NldsPreferred) &&
				    memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
				    memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
					int log_addr = 0;
					if (first_ipv6) {
						memcpy(service_address_ipv6, &saddr->sin6_addr, 16);
						first_ipv6 = 0;
						log_addr = 1;
					}
					has_ipv6 = 1;
					if (num_sockets < max_sockets) {
						saddr->sin6_port = htons((unsigned short)port);
						int sock = mdns_socket_open_ipv6(saddr);
						if (sock >= 0) {
							sockets[num_sockets++] = sock;
							log_addr = 1;
						} else {
							log_addr = 0;
						}
					}
					if (log_addr) {
						char buffer[128];
						mdns_string_t addr = ipv6_address_to_string(buffer, sizeof(buffer), saddr,
						                                            sizeof(struct sockaddr_in6));
					}
				}
			}
		}
	}

	free(adapter_address);

#else

	struct ifaddrs* ifaddr = 0;
	struct ifaddrs* ifa = 0;

	if (getifaddrs(&ifaddr) < 0)
		ssp_blog(LOG_ERROR, "Unable to get interface addresses");

	int first_ipv4 = 1;
	int first_ipv6 = 1;
	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;

		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in* saddr = (struct sockaddr_in*)ifa->ifa_addr;
			if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
				int log_addr = 0;
				if (first_ipv4) {
					service_address_ipv4 = saddr->sin_addr.s_addr;
					first_ipv4 = 0;
					log_addr = 1;
				}
				has_ipv4 = 1;
				if (num_sockets < max_sockets) {
					saddr->sin_port = htons(port);
					int sock = mdns_socket_open_ipv4(saddr);
					if (sock >= 0) {
						sockets[num_sockets++] = sock;
						log_addr = 1;
					} else {
						log_addr = 0;
					}
				}
				if (log_addr) {
					char buffer[128];
					mdns_string_t addr = ipv4_address_to_string(buffer, sizeof(buffer), saddr,
					                                            sizeof(struct sockaddr_in));
				}
			}
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6* saddr = (struct sockaddr_in6*)ifa->ifa_addr;
			static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
			                                          0, 0, 0, 0, 0, 0, 0, 1};
			static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
			                                                 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
			if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
			    memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
				int log_addr = 0;
				if (first_ipv6) {
					memcpy(service_address_ipv6, &saddr->sin6_addr, 16);
					first_ipv6 = 0;
					log_addr = 1;
				}
				has_ipv6 = 1;
				if (num_sockets < max_sockets) {
					saddr->sin6_port = htons(port);
					int sock = mdns_socket_open_ipv6(saddr);
					if (sock >= 0) {
						sockets[num_sockets++] = sock;
						log_addr = 1;
					} else {
						log_addr = 0;
					}
				}
				if (log_addr) {
					char buffer[128];
					mdns_string_t addr = ipv6_address_to_string(buffer, sizeof(buffer), saddr,
					                                            sizeof(struct sockaddr_in6));
				}
			}
		}
	}

	freeifaddrs(ifaddr);

#endif

	return num_sockets;
}


static int
send_mdns_query(const char* service, size_t service_len) {
	static int current_id = 1;
	int sockets[32];
	int query_id[32];
	int num_sockets = open_client_sockets(sockets, sizeof(sockets) / sizeof(sockets[0]), 0);
	if (num_sockets <= 0) {
		ssp_blog(LOG_ERROR, "Failed to open any client sockets");
		return -1;
	}

	size_t capacity = 2048;
	void* buffer = malloc(capacity);
	void* user_data = 0;
	size_t records;
	for (int isock = 0; isock < num_sockets; ++isock) {
		query_id[isock] = mdns_query_send(sockets[isock], MDNS_RECORDTYPE_PTR, service,
		                                  service_len, buffer, capacity, current_id++);
		if (query_id[isock] < 0)
			ssp_blog(LOG_DEBUG, "Failed to send mDNS query: %s", strerror(errno));
	}

	// This is a simple implementation that loops for 5 seconds or as long as we get replies
	int res;
	do {
		struct timeval timeout;
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;

		int nfds = 0;
		fd_set readfs;
		FD_ZERO(&readfs);
		for (int isock = 0; isock < num_sockets; ++isock) {
			if (sockets[isock] >= nfds)
				nfds = sockets[isock] + 1;
			FD_SET(sockets[isock], &readfs);
		}

		records = 0;
		res = select(nfds, &readfs, 0, 0, &timeout);
		if (res > 0) {
			for (int isock = 0; isock < num_sockets; ++isock) {
				if (FD_ISSET(sockets[isock], &readfs)) {
					records += mdns_query_recv(sockets[isock], buffer, capacity, query_callback,
					                           user_data, query_id[isock]);
				}
				FD_SET(sockets[isock], &readfs);
			}
		}
	} while (res > 0);

	free(buffer);

	for (int isock = 0; isock < num_sockets; ++isock)
		mdns_socket_close(sockets[isock]);

	return 0;
}


static void *mdns_loop(void *ptr){
    mdns_args *arg = (mdns_args *)ptr;
    void *buffer = bzalloc(MDNS_BUFFER_SIZE);
    int sock = mdns_socket_open_ipv4(0);
    while(arg->running) {
        send_mdns_query(arg->service_str, arg->service_str_size);
    }
    mdns_socket_close(sock);
    bfree(buffer);
    return nullptr;
}

void create_mdns_loop(){
    ssp_records_lock.lock();
    ssp_records.clear();
    ssp_records_lock.unlock();
    g_mdns_args.service_str = ZCAM_QUERY_DOMAIN;
    g_mdns_args.service_str_size = strlen(ZCAM_QUERY_DOMAIN);
    g_mdns_args.running = true;
    pthread_create(&mdns_thread, nullptr, mdns_loop, (void*)&g_mdns_args);
    ssp_blog(LOG_INFO, "mdns query thread started.");
}

void stop_mdns_loop(){
    ssp_records_lock.lock();
    ssp_records.clear();
    ssp_records_lock.unlock();
    ssp_blog(LOG_INFO, "stop mdns query thread...");
    while(g_mdns_args.running)
        g_mdns_args.running = false;
    pthread_join(mdns_thread, nullptr);
    ssp_blog(LOG_INFO, "mdns query thread stopped.");
}



SspMDnsIterator::SspMDnsIterator() {
    ssp_records_lock.lock();
    iter = ssp_records.begin();
    current_time = os_gettime_ns()/1000000;
}
SspMDnsIterator::~SspMDnsIterator(){
    ssp_records_lock.unlock();
}
bool SspMDnsIterator::hasNext() {
    return iter != ssp_records.end();
}
ssp_device_item* SspMDnsIterator::next(){
    static ssp_device_item ret;
    while (hasNext()){
        if(iter->second.last_available < current_time) {
            ++iter;
            continue;
        }
        ret.device_name = iter->second.ptr_record;
        if(iter->second.has_a) {
            mdns_string_t addr = ipv4_address_to_string(addrbuffer, sizeof(addrbuffer), &(iter->second.a_record), sizeof(iter->second.a_record));
            ret.ip_address = std::string(addr.str, addr.length);
            ++iter;
            return &ret;
        } else if (iter->second.has_aaaa){
            mdns_string_t addr = ipv6_address_to_string(addrbuffer, sizeof(addrbuffer), &(iter->second.aaaa_record), sizeof(iter->second.aaaa_record));
            ret.ip_address = std::string(addr.str, addr.length);
            ++iter;
            return &ret;
        } else {
            ++iter;
            continue;
        }
    }
    return nullptr;
}
