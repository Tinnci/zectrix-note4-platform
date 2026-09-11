# G1.2 GitHub synchronization and delivery review

Reviewed on **2026-09-12**. G1.2 synchronizes the 35 local commits accumulated
after remote `main` at `c8d4e56`, plus the packaging/CI/documentation changes in
this iteration, through [PR #54](https://github.com/Tinnci/zectrix-note4-platform/pull/54).
The PR now covers E1.1–E1.10, E2.1/E2.2, R1.3/R1.4 and D1.4/D1.5.

The existing required Host, Android and firmware checks, strict branch status,
linear history and review-conversation rules remain in place. Rebase merging
preserves the individual changes under the repository's linear-history policy.
GitHub regenerates commit IDs during that merge; release manifests retain the
actual source commit used for the builds.

## Delivery and open qualification

| Object | G1.2 result |
| --- | --- |
| PR #54 | Updated from its E1.1–E1.7 description to the complete terminal scope. The failed-candidate destructor review is fixed by D1.5's callback guard and 4,096-cycle regression; centralized render retry also covers the Settings/Gallery/Device Info findings. |
| #58 and E1 milestone 13 | E1.8's transport study and USB books/settings management are implemented in `36f903c`, extended with Lua app transfer in E2.2. #54 closes the design/delivery issue; the E1 milestone records E1.1–E1.10 completion. |
| #40, #44–#47 and D1 milestone 8 | D1.4 now provides the missing copied status commands, input mirror and confirmed maintenance; D1.5 adds foreground health/recovery. Their broader real USB/confirmation/stream/sleep evidence remains open. |
| #42/#43 | Existing transport/owner implementation remains delivered. Original real reconnect and clean-build/device transcript criteria are retained. |
| #56/#57 | D1.5 adds fault injection and health supervision; R1.4 adds display telemetry/modeling. Simulated faults and normal boot/feeding evidence do not close physical interrupted OTA, optical/power, transfer, sleep or RTC measurements. |
| Research milestone 6 | E2.1 research and E2.2 restricted Lua pilots replace the old deferred-only description. Compiled Wasm/native loading and E2.3 packaging remain future work. |
| R1 milestone 9 | Software scope now includes typography, compact fonts and parameterized display-physics observations/scheduling. Instrument measurements remain #57. |
| C1 milestone 7 | Existing Android/BLE/NFC and RF/current evidence in #29, #34–#39 and #48 remains open. |
| #55 | The draft firmware downloads support preparation; a trusted user-facing firmware delivery/update workflow remains outstanding. |

The bodies and acceptance conditions of the physical follow-ups are preserved.
Issue comments and milestone descriptions record the delivered software and
the precise remaining observations. No retrospective Issue or new release gate
is needed for the existing tracked work.

## Verification and release preparation

The local 40-target Host suite includes the package regression in its existing
firmware-budget target. Full/Minimal artifacts use the qualified ESP-IDF v5.5.2
builds and the existing profile comparison. See
[RELEASE_PREPARATION.md](RELEASE_PREPARATION.md) for ZIP/application downloads,
source manifests, licenses, download checksums and installation scope.

The first updated remote CI run exposed GCC's missing explicit `<cstring>` in
the Platform fixture and a missing `uv` executable in the firmware container.
Both are corrected without relaxing the existing checks. The Android JVM/debug
build passed that run; [CI run 34652131070](https://github.com/Tinnci/zectrix-note4-platform/actions/runs/34652131070)
passed all three jobs on the corrected source `c401486`.

The `v1.2.0-preview.1` release remains a draft. Its source revision and binaries
are preparation artifacts; production publication and the final user handbook
remain R2.1. D1.5 hardware evidence is carried forward with its stated limits;
G1.2 does not claim a new flash or physical soak.

The [draft release](https://github.com/Tinnci/zectrix-note4-platform/releases/tag/untagged-f5a3e8f7c85ed84e2c08)
contains six downloads built from clean source `c401486`: two profile ZIPs,
two application binaries, the combined manifest and SHA256SUMS. Full/Minimal
remain 2,495,312 / 521,648 bytes, with 201,815 / 108,323 bytes of static internal
RAM. The native descriptors agree with `v1.0.0-129-gc401486`; the preview label
does not rewrite the embedded Git-derived version.
All six uploaded assets were downloaded again and passed the checksum list.

The dated G1.1 review below remains the historical inventory. Its statements
about pending E1.8/D1 implementations are superseded by this G1.2 review.

---

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
