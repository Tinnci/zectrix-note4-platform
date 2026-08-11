# Platform migration principles

Status: Draft.

This document defines rules for platform migrations in M2 and M3. The rules
use controlled technical English that is aligned with useful ASD-STE100
principles. The project does not claim formal ASD-STE100 compliance or
certification.

The key words `MUST`, `MUST NOT`, `SHOULD`, and `MAY` show the strength of a
requirement:

- `MUST` identifies a required result.
- `MUST NOT` identifies a prohibited action or result.
- `SHOULD` identifies a recommended result. An Issue must record the reason for
  each exception.
- `MAY` identifies a permitted choice.

## Resource ownership

**OWN-001** Each hardware resource MUST have one platform owner.

**OWN-002** The platform owner MUST control all operations for the resource.

**OWN-003** The platform owner MUST control all logical state for the resource.

**OWN-004** The platform owner MUST control all policy decisions for the
resource.

**OWN-005** The platform owner MUST control the complete lifecycle of the
resource.

**OWN-006** Application code MUST use the platform owner for each resource
operation.

**OWN-007** Application code MUST NOT use a raw driver to bypass the platform
owner.

**OWN-008** A migration MUST include all call sites that access the resource.

**OWN-009** A migration MUST include all state that describes the resource.

## Behavior preservation

**REF-001** A refactor MUST preserve the qualified hardware behavior.

**REF-002** A refactor commit MUST NOT contain an unqualified hardware
optimization.

**REF-003** A change to hardware behavior MUST use a separate change set.

**REF-004** A change to hardware behavior MUST have separate qualification
evidence.

**REF-005** A change to a timing value MUST have separate qualification.

**REF-006** A change to a display waveform MUST have separate qualification.

**REF-007** A change to GPIO operation order MUST have separate qualification.

**REF-008** A change to a power sequence MUST have separate qualification.

**REF-009** A change to BUSY signal handling MUST have separate qualification.

## Architecture enforcement

**ARCH-001** The architecture checker MUST scan each current application source
root.

**ARCH-002** The architecture checker MUST scan the source tree that the build
uses.

**ARCH-003** The architecture checker MUST reject each prohibited dependency.

**ARCH-004** Each temporary exception MUST identify one exact file.

**ARCH-005** Each temporary exception MUST identify one rule.

**ARCH-006** Each temporary exception MUST identify the Issue that removes the
exception.

**ARCH-007** An allowlist MUST NOT exempt a complete source directory.

**ARCH-008** A migration MUST remove each obsolete allowlist entry.

## Test levels

The project uses five test levels. A higher level does not replace a lower
level when the acceptance criteria require both levels.

1. **Level 1 — Compile and API test.** This test confirms that an interface can
   compile.
2. **Level 2 — State and unit test.** This test confirms required state
   transitions and local invariants.
3. **Level 3 — Mapping and semantic test.** This test confirms required input,
   output, timeout, error, and policy mappings.
4. **Level 4 — Integration test.** This test confirms the interaction between
   the service, its lower layer, and its consumers.
5. **Level 5 — Hardware regression.** This test confirms the required behavior
   on the specified hardware baseline.

**TEST-001** Each Issue MUST identify the required test levels.

**TEST-002** Each semantic requirement MUST have a Level 2 or Level 3 test when
a host test can verify the requirement.

**TEST-003** Each hardware-sensitive change MUST pass the applicable Level 5
test.

**TEST-004** Each Level 5 result MUST identify the firmware commit.

**TEST-005** Each Level 5 result MUST identify the hardware baseline.

**TEST-006** Each migration MUST pass the architecture checker.

**TEST-007** Each migration MUST pass a full clean build before hardware
qualification.

## Completion and stability

**PM-001** An Issue MUST close only after its current acceptance criteria pass.

**PM-002** Issue closure MUST NOT mean that an API is stable.

**PM-003** A Milestone MUST close only after its exit gate passes.

**PM-004** Interfaces in M2 and M3 MUST have Draft status.

**PM-005** M4 MUST define the SDK v1 compatibility policy.

**PM-006** M4 MUST state whether SDK v1 gives source compatibility.

**PM-007** M4 MUST state whether SDK v1 gives binary ABI compatibility.

**PM-008** The project MUST NOT claim binary ABI stability before M4 makes this
decision.

## Change isolation

**CHG-001** One qualified change set MUST change only one uncertainty class.

**CHG-002** An architecture migration MUST NOT include a waveform optimization.

**CHG-003** An architecture migration MUST NOT include a toolchain upgrade.

**CHG-004** A waveform change MUST have its own measurements and qualification.

**CHG-005** A toolchain change MUST have its own build and hardware
qualification.

An Issue MAY contain more than one commit. Each commit SHOULD keep one clear
purpose.

## Standard migration process

Do these steps for each platform resource:

1. Define the resource.
2. Identify one platform owner.
3. Record the qualified hardware behavior.
4. Add or modify the platform service.
5. Migrate all call sites.
6. Migrate all resource state and policy.
7. Search the real source tree for bypass paths.
8. Remove obsolete allowlist entries.
9. Run the required Level 1 to Level 4 tests.
10. Run the architecture checker.
11. Run a full clean build.
12. Run the required Level 5 hardware regression.
13. Record the firmware commit and the test evidence.
14. Close the Issue only when all current acceptance criteria pass.

Do not combine a refactor with a hardware behavior change. Create a separate
Issue or change set for the hardware behavior change.

## Issue closure checklist

- [ ] The Issue defines the resource.
- [ ] The Issue identifies one platform owner.
- [ ] The owner controls operations, state, policy, and lifecycle.
- [ ] All application call sites use the owner.
- [ ] No bypass path remains.
- [ ] The migration preserves the qualified hardware behavior.
- [ ] Each behavior change has separate qualification.
- [ ] The Issue identifies the required test levels.
- [ ] The required unit and semantic tests pass.
- [ ] The architecture checker scans the real source roots and passes.
- [ ] Each allowlist entry identifies an exact file, rule, and removal Issue.
- [ ] The full clean build passes.
- [ ] The required hardware regression passes.
- [ ] The result identifies the firmware commit and hardware baseline.
- [ ] Follow-up work is separate from current acceptance.
- [ ] The Issue does not claim API or ABI stability unless M4 permits the claim.
