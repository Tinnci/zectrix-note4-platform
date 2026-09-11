#pragma once
#include <cstdint>

struct wdt_hal_context_t {};
#define RWDT_HAL_CONTEXT_DEFAULT() {}
enum wdt_stage_t { WDT_STAGE0 };
enum wdt_stage_action_t { WDT_STAGE_ACTION_RESET_RTC };

void wdt_hal_write_protect_disable(wdt_hal_context_t*);
void wdt_hal_write_protect_enable(wdt_hal_context_t*);
void wdt_hal_config_stage(wdt_hal_context_t*, wdt_stage_t, uint32_t, wdt_stage_action_t);
void wdt_hal_enable(wdt_hal_context_t*);
void wdt_hal_feed(wdt_hal_context_t*);
void wdt_hal_disable(wdt_hal_context_t*);
