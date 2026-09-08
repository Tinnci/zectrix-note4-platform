#pragma once

#include "ip_addr.h"

using err_t = int;
constexpr err_t ERR_OK = 0, ERR_MEM = -1, ERR_INPROGRESS = -5;
constexpr uint8_t LWIP_DNS_ADDRTYPE_IPV4 = 0;
using dns_found_callback = void (*)(const char*, const ip_addr_t*, void*);
err_t dns_gethostbyname_addrtype(const char*, ip_addr_t*, dns_found_callback, void*, uint8_t);
