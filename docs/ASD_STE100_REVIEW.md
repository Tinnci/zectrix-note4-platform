# ASD-STE100 documentation review

Review date: 2026-08-11

## Result

The documents in the selected scope are aligned with useful ASD-STE100 Issue 9
principles. The project does not pursue formal compliance or certification.

The review inspected all tracked Markdown files to find consistency and safety
issues. The mandatory policy applies only to operation, recovery, toolchain,
test and API documents. The review gave extra attention to the documents
changed by the STE alignment and U0 qualification commits:

- `1eb64ae` — documentation style alignment.
- `20bb282` — first-flash qualification record.
- `472c918` — first-flash result.
- `24ba37f` — display qualification result.
- `dd84d95` — hardware qualification result.
- `adabc26` — factory recovery result.

## What already works

- Setup, test and recovery instructions usually use an imperative sentence.
- The reviewed records use active voice when the operator or tool is known.
- Hashes, sizes, offsets, versions and result states use exact values.
- The qualification record separates evidence from subjective display quality.
- The policy already excludes commands, source code, paths, hashes and
  identifiers from natural-language rewriting.

## Gaps

The project does not require a complete Issue 9 dictionary review or an
approved glossary for all technical nouns. A manual scan found recurring word
choices for which Issue 9 gives clearer alternatives in operational prose:

| Current wording | Preferred wording | Action |
| --- | --- | --- |
| perform | do | Changed in procedure and API guidance text. |
| using | use or with | Changed where the word describes an action. |
| may | can | Changed where the word means possibility. |
| follow | do the steps in | Changed for an operator instruction. |
| ensure | make sure | Keep as a review rule. No current prose occurrence remains. |
| check (verb) | confirm or do a check | Changed in prerequisite guidance. |
| any | a, each or a different construction | Changed where it added ambiguity. |
| complete (descriptive adjective) | full or successful | Changed in operator records. Keep exact status labels. |

The original README warning also used `IMPORTANT` for a flash operation. The
review adds a `CAUTION` signal with an action and a possible result. This is a
justified safety improvement.

## Decision

Use this policy:

1. Apply the mandatory review to setup, flashing, recovery, toolchain, test,
   qualification and API documents.
2. Use the same clarity rules in other documents only when the result is easier
   to understand and technically accurate.
3. Do not run a full repository rewrite. The cost is not justified for source
   code, commands, hashes, paths, product names or established technical nouns.
4. Do not pursue or claim formal ASD-STE100 compliance or certification.

This decision preserves readability while reducing ambiguity in the documents
that can change hardware state or determine a qualification result.

## Acceptance checks

For future changes to strict-scope documents:

- use one action per procedural sentence.
- use an active imperative when the operator is the agent.
- use exact units, values and result states.
- use `WARNING` or `CAUTION` for safety instructions.
- do not add semicolons to prose.
- keep technical terms stable.
- record an intentional terminology exception when it can affect meaning.

Reference: [ASD-STE100 Issue 9](https://www.asd-ste100.org/assets/files/ASD-STE100_ISSUE9.pdf).
