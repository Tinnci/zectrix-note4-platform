#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

struct zectrix_epd_t;
using zectrix_epd_handle_t = zectrix_epd_t*;
struct zectrix_epd_config_t {};
struct zectrix_epd_rect_t { int x; int y; int width; int height; };

void zectrix_epd_get_default_config(zectrix_epd_config_t* config);
esp_err_t zectrix_epd_new(const zectrix_epd_config_t* config,
                          zectrix_epd_handle_t* out_handle);
esp_err_t zectrix_epd_del(zectrix_epd_handle_t handle);
esp_err_t zectrix_epd_power_on(zectrix_epd_handle_t handle);
esp_err_t zectrix_epd_power_off(zectrix_epd_handle_t handle);
bool zectrix_epd_is_powered(zectrix_epd_handle_t handle);
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t handle,
                                        const std::uint8_t* framebuffer,
                                        std::size_t size);
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t handle,
                                           const zectrix_epd_rect_t* rect,
                                           const std::uint8_t* pixels,
                                           std::size_t size);
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t handle,
                                        const std::uint8_t* framebuffer,
                                        std::size_t size);
