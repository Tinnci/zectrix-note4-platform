# G1.1 GitHub Issues and Milestones review

Reviewed on **2026-09-10** for
[Tinnci/zectrix-note4-platform](https://github.com/Tinnci/zectrix-note4-platform).
The review compared Issue bodies and comments with implementation, contracts,
commit history and recorded verification. It also ran the current Host suite.

Remote `main` was `c8d4e56`. The E1.1–E1.7 implementation through `ea09f7e`
was already published in [PR #54](https://github.com/Tinnci/zectrix-note4-platform/pull/54),
whose head became `bcc85b2` with the E1.8 planning updates. Those updates were
preserved. The PR remains open. Its title, delivery description and milestone
now include the implemented E1.1–E1.7 scope and identify E1.8 as future work.

## Result

| Object | Before review | After review |
| --- | --- | --- |
| Issues, excluding pull requests | 47 total: 16 open, 31 closed | 55 total: 19 open, 36 closed |
| Milestones | 8 total: 4 open, 4 closed | 13 total: 4 open, 9 closed |

All 16 previously open Issues received an individual delivery review. The
resolved architecture decision #7 was closed as completed. Four retrospective
delivery records, [#59](https://github.com/Tinnci/zectrix-note4-platform/issues/59),
[#60](https://github.com/Tinnci/zectrix-note4-platform/issues/60),
[#61](https://github.com/Tinnci/zectrix-note4-platform/issues/61) and
[#62](https://github.com/Tinnci/zectrix-note4-platform/issues/62), were created
and closed. Four remaining tasks,
#55–#58, were made explicit. The 31 previously closed Issues were unchanged.

## Milestone decisions

| Milestone | State after review | Recorded scope |
| --- | --- | --- |
| M1–M4, numbers 1–4 | Closed, unchanged | Existing acceptance records remain intact. |
| [M5, number 5](https://github.com/Tinnci/zectrix-note4-platform/milestone/5) | Closed | #7's measured A/B architecture decision is complete. Delivery and physical rollback remain in #55/#56. |
| [C1, number 7](https://github.com/Tinnci/zectrix-note4-platform/milestone/7) | Open | Software paths are implemented. The existing Android/Note4 and RF/power criteria in #29/#38 and their children remain outstanding. |
| [D1, number 8](https://github.com/Tinnci/zectrix-note4-platform/milestone/8) | Open | USB and diagnostic slices are delivered. #44–#46 still contain implementation work, and #42–#47 retain their physical evidence requirements. |
| [R1, number 9](https://github.com/Tinnci/zectrix-note4-platform/milestone/9) | Closed | R1.1/R1.2 software delivery is recorded in #59. Panel measurements remain in #57. |
| [Q1, number 10](https://github.com/Tinnci/zectrix-note4-platform/milestone/10) | Closed | Q1.1–Q1.3 regression, lifecycle and protocol audits are recorded in #60. |
| [L1, number 11](https://github.com/Tinnci/zectrix-note4-platform/milestone/11) | Closed | L1.1–L1.4 launcher, reader, transfer and sleep implementation is recorded in #61. Physical follow-ups remain in #57/#37/#38. |
| [S1, number 12](https://github.com/Tinnci/zectrix-note4-platform/milestone/12) | Closed | S1.1–S1.4 service/build/RTC implementation and profile verification is recorded in #62. Retention/current measurements remain in #57/#37. |
| [E1, number 13](https://github.com/Tinnci/zectrix-note4-platform/milestone/13) | Open | PR #54 awaits integration. E1.8 architecture exploration remains open in #58. |
| [Research, number 6](https://github.com/Tinnci/zectrix-note4-platform/milestone/6) | Open, deferred | Renamed from R1 to Research — Dynamic application runtime, preserving the original ELF/WebAssembly research scope. |

The old research milestone was not reused for display work. New R1/Q1/L1/S1
milestones record the completed software scope in `ASTRA_TASKS.md`. Their
separate physical follow-ups preserve the limitations already documented at
delivery. C1/D1 keep their original, broader closure conditions.

## Issue decisions

Backlog numbering and historical GitHub numbering differ. For example,
`ASTRA_TASKS.md` D1.2 delivers diagnostics and log observation, while GitHub
D1.2 (#42) denotes the USB transport. A completed local slice is not sufficient
to close a broader Issue. The production CLI table confirms that several
commands in the original D1 contract are still future work.

| Existing Issue | Decision | Delivery and remaining work |
| --- | --- | --- |
| [#7](https://github.com/Tinnci/zectrix-note4-platform/issues/7), M5 decision | Closed, completed | ADR-0005 measures firmware/assets/NVS, selects factory plus two writable OTA slots and defines rollback/compatibility. M5.1/M5.2 implement the service. #55/#56 track the remaining product workflow and physical evidence. |
| [#29](https://github.com/Tinnci/zectrix-note4-platform/issues/29), C1 umbrella | Open | Direct Wi-Fi/HTTPS, durable replay, authorization and cleanup are implemented. Child physical criteria and #38 remain outstanding. |
| [#34](https://github.com/Tinnci/zectrix-note4-platform/issues/34), secure BLE | Open | Production transport, persisted bonds and session handling are implemented. Fresh pairing, encrypted reboot reconnect, unpair/re-pair, malformed peer frames and soak need the specified real-device evidence. |
| [#35](https://github.com/Tinnci/zectrix-note4-platform/issues/35), phone resource gateway | Open | The controlled HTTPS path is implemented. Real Note4/Android fetch, offline queue and reconnect delivery remain unrecorded. |
| [#36](https://github.com/Tinnci/zectrix-note4-platform/issues/36), direct Wi-Fi | Open | ESP driver, verified HTTPS and cleanup are implemented. Real association/resource/fallback, preserved RF diagnostics and radio-stop evidence remain. |
| [#37](https://github.com/Tinnci/zectrix-note4-platform/issues/37), power/coexistence | Open | Lifecycle and E1.5 arbitration code is delivered. Actual RF/current/latency and resource-soak measurements remain. |
| [#38](https://github.com/Tinnci/zectrix-note4-platform/issues/38), C1 end-to-end qualification | Open | Software regression passes. #34–#37, #39 and #48 retain their named physical requirements. |
| [#39](https://github.com/Tinnci/zectrix-note4-platform/issues/39), Android companion | Open | GATT, enrollment/authorization, resource gateway, durable progress and time calibration are implemented. Physical association, background/process lifecycle and reconnect evidence remain. |
| [#48](https://github.com/Tinnci/zectrix-note4-platform/issues/48), NFC enrollment | Open | NDEF/bootstrap, single-use token verification and stored peer identity are implemented. Real Android NFC routing, enrollment, replay/expiry and re-enrollment evidence remain. |
| [#40](https://github.com/Tinnci/zectrix-note4-platform/issues/40), D1 umbrella | Open | Local D1.1–D1.3 is delivered; the broader GitHub command/qualification scope is incomplete. |
| [#42](https://github.com/Tinnci/zectrix-note4-platform/issues/42), USB session | Open | Session/history/reconnect/cleanup implementation passes Host tests. A real unplug/replug and prompt-recovery transcript is still required. |
| [#43](https://github.com/Tinnci/zectrix-note4-platform/issues/43), owner dispatcher | Open | Fixed typed requests, generations, copied results and owner execution are implemented. The required real Note4 system-info transcript remains. |
| [#44](https://github.com/Tinnci/zectrix-note4-platform/issues/44), status commands | Open | Heap/display commands exist. Power, time, connectivity, app-list and current-app commands are absent from the production table. |
| [#45](https://github.com/Tinnci/zectrix-note4-platform/issues/45), observation streams | Open | Bounded log streaming exists. The non-consuming InputEvent mirror and input watch remain unimplemented; USB evidence is also required. |
| [#46](https://github.com/Tinnci/zectrix-note4-platform/issues/46), confirmed mutations | Open | Access/origin definitions exist. End-to-end physical confirmation and production mutation commands remain unimplemented. |
| [#47](https://github.com/Tinnci/zectrix-note4-platform/issues/47), D1 qualification | Open | Depends on the remaining implementations and physical USB/confirmation/sleep sequence. Boot-only CLI startup is partial evidence. |

## Remaining work

| Issue | Scope carried forward |
| --- | --- |
| [#55](https://github.com/Tinnci/zectrix-note4-platform/issues/55) | Trusted firmware source, update transport and user workflow using the existing bounded UpdateService. CRC does not establish publisher identity. |
| [#56](https://github.com/Tinnci/zectrix-note4-platform/issues/56) | Real interrupted writes, native readback, trial timeout, interrupted confirmation and power-loss rollback in both OTA directions. |
| [#57](https://github.com/Tinnci/zectrix-note4-platform/issues/57) | Physical panel/reader/transfer controls, sleep/wake, RTC backup retention and standby observations. Reuse #37/#38 for shared RF/current/Android evidence. |
| [#58](https://github.com/Tinnci/zectrix-note4-platform/issues/58) | E1.8 USB host-communication architecture, session/data transfer trade-offs and coordinated device interaction. The transport choices remain open. |

#55–#57 carry forward explicit limitations from existing delivery documents.
#58 mirrors the newly added E1.8 planning item without implementing it or
marking the missing D1 commands complete.

## Sources and verification

The review used [ASTRA_TASKS.md](../ASTRA_TASKS.md),
[ADR-0005](adr/0005-ab-ota-boot-confirmation.md), the
[connectivity](CONNECTIVITY_CONTRACT.md), [CLI](MAINTENANCE_CLI_CONTRACT.md)
and [application](M3_APPLICATION_CONTRACT.md) contracts, the
[production CLI descriptors](../components/zectrix_cli/zectrix_cli_diagnostics.cc),
and the [reader](READER.md), [transfer](BOOK_TRANSFER.md),
[sleep](SLEEP_COVER.md), [RTC](TIME.md), [modular build](MODULAR_BUILD.md)
and [localization](LOCALIZATION.md) delivery records. The
[CrossPoint/Flipper study](FIRMWARE_UI_STUDY.md) supplies the reference context
for streamed reading/transfer, static covers and scene/view ownership.

`bash tools/test-host.sh` passed all **34 targets**, including architecture
checks, the SDK consumer, driver seams, streamed reading/transfer, CLI and
localization. The local log is `build-governance-g1-1/host.log` (ignored).
This governance iteration changed documentation and remote tracking. It did
not run a firmware build, flash, physical measurement or Android device test.
Earlier build/sanitizer/boot results are cited as recorded evidence only.

After the mutations, GitHub was read again to verify all Issue states and
milestone assignments, the 13 milestone states/descriptions, and PR #54's
title, body, open state and E1 assignment. The five closed Issues use the
`completed` reason. Existing open physical/security criteria were retained.

For a later review, retrieve the actual remote inventory and compare each
Issue's scope with current implementation and evidence:

```bash
gh issue list --repo Tinnci/zectrix-note4-platform --state all --limit 500 \
  --json number,title,state,milestone,url
gh api --paginate repos/Tinnci/zectrix-note4-platform/milestones \
  --method GET -f state=all -f per_page=100
gh pr view 54 --repo Tinnci/zectrix-note4-platform \
  --json title,state,headRefOid,milestone
```
