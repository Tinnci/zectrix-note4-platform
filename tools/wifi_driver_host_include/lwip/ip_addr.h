#pragma once

#include <cstdint>

struct ip_addr_t { uint32_t addr = 0; bool v4 = true; };
inline bool IP_IS_V4(const ip_addr_t* address) { return address->v4; }
inline bool ip_addr_isany(const ip_addr_t* address) { return address->addr == 0; }
const char* ipaddr_ntoa_r(const ip_addr_t*, char*, int);
