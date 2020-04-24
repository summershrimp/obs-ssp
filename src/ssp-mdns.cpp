//
// Created by xm1994 on 2020/4/26.
//

#include <map>
#include <string>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mdns.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

#include "obs-ssp.h"
#include "ssp-mdns.h"

#define DEFAULT_TTL 300

struct mdns_args {
    bool running;
    const char *service_str;
    size_t service_str_size;
} g_mdns_args;


uint16_t current_transaction_id;
struct mdns_record current_mdns_record;

pthread_t mdns_thread;

#define MDNS_BUFFER_SIZE 2048

static char addrbuffer[64];
static char namebuffer[256];
static char sendbuffer[256];
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
    mdns_string_t str = {buffer, len};
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
    mdns_string_t str = {buffer, len};
    return str;
}

static mdns_string_t
ip_address_to_string(char* buffer, size_t capacity, const struct sockaddr* addr, size_t addrlen) {
    if (addr->sa_family == AF_INET6)
        return ipv6_address_to_string(buffer, capacity, (const struct sockaddr_in6*)addr, addrlen);
    return ipv4_address_to_string(buffer, capacity, (const struct sockaddr_in*)addr, addrlen);
}


static int
query_callback(int sock, const struct sockaddr* from, size_t addrlen,
               mdns_entry_type_t entry, uint16_t transaction_id,
               uint16_t rtype, uint16_t rclass, uint32_t ttl,
               const void* data, size_t size, size_t offset, size_t length,
               void* user_data) {
    if(transaction_id != current_transaction_id) {
        current_mdns_record.has_ptr = false;
        current_mdns_record.has_a = false;
        current_mdns_record.has_aaaa = false;
        current_transaction_id = transaction_id;
    }
    ttl = DEFAULT_TTL; // The mdns library we use cannot query all interface so we use a longer ttl.
    if (rtype == MDNS_RECORDTYPE_PTR) {
        mdns_string_t ptr_str = mdns_record_parse_ptr(data, size, offset, length,
                                                      namebuffer, sizeof(namebuffer));
        std::string ptr(ptr_str.str, ptr_str.length - strlen(ZCAM_QUERY_DOMAIN) - 2);
        current_mdns_record.ptr_record = ptr;
        current_mdns_record.last_available = os_gettime_ns() / 1000000 + ttl * 1000;
        current_mdns_record.has_ptr = true;
    }
    else if (current_mdns_record.has_ptr && rtype == MDNS_RECORDTYPE_A && from->sa_family == AF_INET) {
        struct sockaddr_in addr;
        mdns_record_parse_a(data, size, offset, length, &addr);
        memcpy(&(current_mdns_record.a_record), &addr, sizeof(current_mdns_record.a_record));
        current_mdns_record.has_a = true;
        current_mdns_record.last_available = os_gettime_ns() / 1000000 + ttl * 1000;
        ssp_records_lock.lock();
        ssp_records[current_mdns_record.ptr_record] = current_mdns_record;
        ssp_records_lock.unlock();
    }
    else if (current_mdns_record.has_ptr && rtype == MDNS_RECORDTYPE_AAAA && from->sa_family == AF_INET6) {
        struct sockaddr_in6 addr;
        mdns_record_parse_aaaa(data, size, offset, length, &addr);
        memcpy(&(current_mdns_record.a_record), &addr, sizeof(current_mdns_record.a_record));
        current_mdns_record.has_aaaa = true;
        current_mdns_record.last_available = os_gettime_ns() / 1000000 + ttl * 1000;
        ssp_records_lock.lock();
        ssp_records[current_mdns_record.ptr_record] = current_mdns_record;
        ssp_records_lock.unlock();
    }
    blog(LOG_INFO, "%lu last available:",
         current_mdns_record.last_available);
    return 0;
}


static void *mdns_loop(void *ptr){
    mdns_args *arg = (mdns_args *)ptr;
    void *buffer = bzalloc(MDNS_BUFFER_SIZE);
    size_t records;
    int sock = mdns_socket_open_ipv4(0);
    while(arg->running) {

        mdns_query_send(sock, MDNS_RECORDTYPE_PTR, arg->service_str, arg->service_str_size, buffer, MDNS_BUFFER_SIZE);
        for (int i = 0; i < 5; ++i) {
            do {
                records = mdns_query_recv(sock, buffer, MDNS_BUFFER_SIZE, query_callback, nullptr, 0);
            } while (records);
            if (records) {
                i = 0;
            }
            os_sleep_ms(100);
        }
        os_sleep_ms(1000);
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
    blog(LOG_INFO, "mdns query thread started.");
}

void stop_mdns_loop(){
    ssp_records_lock.lock();
    ssp_records.clear();
    ssp_records_lock.unlock();
    blog(LOG_INFO, "stop mdns query thread...");
    while(g_mdns_args.running)
        g_mdns_args.running = false;
    pthread_join(mdns_thread, nullptr);
    blog(LOG_INFO, "mdns query thread stopped.");
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