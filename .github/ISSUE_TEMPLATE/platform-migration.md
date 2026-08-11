---
name: Platform migration
about: Migrate one hardware resource to one platform owner
title: "M2.x — Migrate <resource> ownership"
labels: "area:platform"
assignees: ""
---

## Objective

Identify the hardware resource and the required result.

Resource:

Platform owner:

Current acceptance result:

## Scope

List the call sites, state, policy, and lifecycle operations in this migration.

## Qualified behavior

Identify the current hardware baseline. Record each timing value, waveform,
GPIO operation order, power sequence, or BUSY rule that this refactor must
preserve.

## Test levels

Select each required level. Give a reason for each level that is not required.

- [ ] Level 1 — Compile and API test
- [ ] Level 2 — State and unit test
- [ ] Level 3 — Mapping and semantic test
- [ ] Level 4 — Integration test
- [ ] Level 5 — Hardware regression

## Acceptance checklist

- [ ] The Issue defines the resource.
- [ ] The Issue identifies one platform owner.
- [ ] The owner controls operations, state, policy, and lifecycle.
- [ ] All application call sites use the owner.
- [ ] No bypass path remains.
- [ ] The refactor preserves the qualified hardware behavior.
- [ ] Each behavior change uses a separate change set and qualification.
- [ ] The required unit and semantic tests pass.
- [ ] The architecture checker scans the real source roots and passes.
- [ ] Each allowlist entry identifies an exact file, rule, and removal Issue.
- [ ] Each obsolete allowlist entry is removed.
- [ ] The full clean build passes.
- [ ] The required hardware regression passes.
- [ ] The result identifies the firmware commit and hardware baseline.
- [ ] Follow-up work is separate from current acceptance.
- [ ] The Issue does not claim API or ABI stability unless M4 permits the claim.

## Evidence

Firmware commit:

Hardware baseline:

Test results:

Architecture check result:

Full clean build result:

Temporary exceptions and removal Issues:

## Stability statement

Issue closure means that the current acceptance criteria pass. It does not
freeze the API or ABI. M2 and M3 interfaces have Draft status. M4 defines the
SDK v1 compatibility policy.

See `docs/PLATFORM_MIGRATION_PRINCIPLES.md`.
