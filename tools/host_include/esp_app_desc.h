#pragma once
#include <cstdint>
struct esp_app_desc_t {
    char project_name[32];
    char version[32];
    char time[16];
    char date[16];
    char idf_ver[32];
    uint8_t app_elf_sha256[32];
};
const esp_app_desc_t* esp_app_get_description();
