#pragma once

#include "ISspClient.h"
namespace imf {
extern "C" {
ISspClient_class *create_ssp_class(const std::string &ip, Loop *loop,
				   size_t bufSize, unsigned short port,
				   uint32_t streamStyle);
ILoop_class *create_loop_class();
}
}