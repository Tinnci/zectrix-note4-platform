# Controlled technical English policy

## Decision

The project uses ASD-STE100-aligned technical English for operational
documentation. This policy does not claim formal ASD-STE100 compliance.

Apply the policy to setup, flashing, recovery, toolchain, test, API and
hardware documents. Do not rewrite source code, shell commands, hashes, paths,
identifiers or protocol names as natural language.

## Why we use it

- Short procedural sentences reduce errors during setup, flashing and recovery.
- Fixed terms make version, hardware and test records easier to compare.
- Active voice makes the responsible operator or tool clear.
- Exact values improve reproducibility and reduce interpretation.
- A controlled style makes later translation and review easier.

## Costs and limits

- Authors need more time to choose exact words and split long sentences.
- Some architecture and API explanations can sound less natural.
- The controlled dictionary does not cover every project-specific identifier.
- A style pass cannot replace hardware tests, build checks or recovery drills.

These costs are justified for safety-sensitive and reproducibility-sensitive
documents. They are not justified for comments, commit messages or code syntax.

## Rules for this repository

1. Use one action per procedural sentence.
2. Use imperative sentences for operator instructions.
3. Use active voice when the responsible agent is known.
4. Use exact numbers and named states instead of vague frequency or modal words.
5. Do not use semicolons in prose. Split the sentence.
6. Use American English spelling.
7. Keep project terms stable. Define a term before using a shortened form.

Reviewers must treat this policy as a readability and safety gate. They must
not use it as a substitute for formal ASD-STE100 dictionary verification.
