//
// Created by xm1994 on 2020/4/26.
//

#ifndef OBS_SSP_SSP_MDNS_H
#define OBS_SSP_SSP_MDNS_H
#include <mdns.h>

#include <string>
#include <map>

struct ssp_device_item {
    std::string device_name;
    std::string ip_address;
};


struct mdns_record {
    bool has_ptr;
    std::string ptr_record;
    bool has_a;
    sockaddr_in a_record;
    bool has_aaaa;
    sockaddr_in6 aaaa_record;
    uint64_t last_available;
};


class SspMDnsIterator{
public:
    SspMDnsIterator();
    ~SspMDnsIterator();
    bool hasNext();
    ssp_device_item* next();

private:
    uint64_t current_time;
    std::map<std::string, mdns_record>::const_iterator iter;
};


void create_mdns_loop();
void stop_mdns_loop();
#endif //OBS_SSP_SSP_MDNS_H
