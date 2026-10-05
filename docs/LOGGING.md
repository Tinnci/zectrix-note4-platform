# Component logging

`note4_log` owns structured events, producer verbosity and RAM capture. It has
no CLI, display, storage or connectivity dependency. The CLI is one consumer;
the old `note4_cli_log.h` is a compatibility alias, not the log-ring owner.

Project components use `NOTE4_LOGE/W/I/D(tag,event,fields,...)`, lowercase tags
and single-line `event=name key=value` records. Prefer integers, explicit units,
`reason`, `result`, session/generation over prose. Untrusted strings use bounded
ASCII `Token` encoding: whitespace, controls and delimiters are percent-encoded;
`~` marks shortening. Never log credentials, tokens, keys, NFC UID bytes or
imported guest payloads.

Default producer level is INFO. Project components retain DEBUG at compile
time without enabling SDK-wide DEBUG. `log level debug` changes only the fixed
project tag list, in RAM, never SDK Wi-Fi/BLE verbosity or NVS.
`log follow debug display` filters capture; it does not enable DEBUG production.
`log stats` reports producer level, queued/dropped/truncated and latest sequence.
Only one active observer is supported.

During USB maintenance, the ESP adapter formats at most 256 bytes on the producer
stack and inserts without waiting for terminal I/O. The 32-record ring holds
224-byte payloads, level, uptime, tag and sequence. Overwrite/contention drops
are counted, long records marked, terminal controls escaped. No log heap or
flash storage. The session drains bounded batches and reports losses.

Capture storage has process lifetime. Start/stop restore the previous ESP sink;
late callbacks cannot reference a destroyed USB service. Early boot, panic,
ISR and cache-disabled code retain ESP-IDF's dedicated paths; normal events are
task-context only. This is neither lossless auditing nor persisted history.

Display completion logs are DEBUG; failures are WARN with kind, reason, error,
BUSY and native RAM traffic. The sixteen-frame telemetry ring provides detailed
measurements, never per-pixel logging. Storage logs NVS error codes, no settings
keys/values; expected missing settings are silent. High-rate producers should
emit state changes or aggregated results, not every poll.

Tests cover escaped/truncated records, sequences, overwrite/contention,
capture lifecycle, exact tag filters and producer/observer level separation.
