#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Pre-instantiation policy, not a replacement for the engine's Wasm validator.
// Reject implicit guest execution before its instruction budget can be armed.
typedef enum { NOTE4_WASM_ACCEPT, NOTE4_WASM_INVALID, NOTE4_WASM_AUTO_INIT } note4_wasm_admission_t;
note4_wasm_admission_t note4_wasm_admit(const uint8_t* bytes, size_t size);
#ifdef __cplusplus
}
#endif
