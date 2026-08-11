# Controlled technical English policy

## Decision

The project uses an ASD-STE100 Issue 9 aligned style for selected English
technical documents. The project does not pursue formal compliance or
certification.

This policy improves clarity, accuracy and consistency. It does not require a
complete audit against the Issue 9 dictionary. It also does not require an
approved glossary for all project terms.

Apply the policy to the document types in the scope below.

## Why we use it

- Short procedural sentences reduce errors during setup, flashing and recovery.
- Fixed terms make version, hardware and test records easier to compare.
- Active voice makes the responsible operator or tool clear.
- Exact values improve reproducibility and reduce interpretation.
- A controlled style makes later translation and review easier.

## Costs and limits

- Authors spend more time to choose exact words and split long sentences.
- Some API explanations can sound less natural.
- The controlled dictionary does not cover every project-specific identifier.
- A style pass cannot replace hardware tests, build checks or recovery drills.

These costs are justified for safety-sensitive and reproducibility-sensitive
documents. They are not justified for comments, commit messages or code syntax.

## Scope

Apply this policy to:

- setup and prerequisite procedures.
- flashing and factory recovery procedures.
- toolchain procedures and policies.
- hardware test criteria.
- qualification records.
- API documentation.

Do not rewrite shell commands, source code, hashes, paths, identifiers, product
names or protocol names. Treat these items as exact technical content.

README files, architecture notes, contribution guides and release notes are not
in the mandatory scope. Authors can use the same clarity rules when the result
is easier to understand and technically accurate.

## Rules for this repository

1. Use one action per procedural sentence.
2. Use imperative sentences for operator instructions.
3. Use active voice when the responsible agent is known.
4. Use exact numbers and named states instead of vague frequency or modal words.
5. Do not use semicolons in prose. Split the sentence.
6. Use American English spelling.
7. Keep project terms stable. Define a term before you use a shortened form.
8. Prefer the Issue 9 alternatives in operational prose. For example, use
   `do` instead of `perform`, `with` or `use` instead of `using`, `can` instead
   of `may`, `obey` instead of `follow`, and `make sure` instead of `ensure`.
9. Start a safety instruction with `WARNING` or `CAUTION`. State the action,
   then state the possible result.

Reviewers must use this policy as a readability and safety gate. They must not
report formal ASD-STE100 compliance or certification.

## Review result for the current documentation set

The 2026-08-11 review inspected the tracked Markdown files. It applied the
mandatory checks to the documents in scope. It gave additional attention to
the documents changed by commits `1eb64ae`, `20bb282`, `472c918`, `24ba37f`,
`dd84d95` and `adabc26`. The result is:

| Area | Result | Decision |
| --- | --- | --- |
| Short sentences and active voice | PASS with minor exceptions | Keep the rule and fix high-risk procedure text first. |
| Imperative operator instructions | PASS in setup, test and recovery text | Keep. |
| American spelling and stable terminology | PASS for reviewed terms | Add a small project glossary when M2 work starts. |
| Semicolon restriction | PASS in mandatory scope | Keep semicolons in source code only. |
| Approved-word dictionary | NOT REQUIRED | Do not claim formal compliance. Use the Issue 9 alternatives when they improve clarity. |
| Safety signal words | PASS for identified risk | Use `CAUTION` for the destructive flash instruction. |
| Commands, identifiers and technical nouns | INTENTIONAL EXCEPTION | Preserve exact spelling and syntax. |
| Chinese documentation | OUT OF SCOPE | Review Chinese text for technical clarity separately. |

This result justifies targeted wording changes. It does not justify a full
rewrite of the repository or a formal compliance program.

The reference is [ASD-STE100 Issue 9](https://www.asd-ste100.org/assets/files/ASD-STE100_ISSUE9.pdf),
published on 2025-01-15. The official site describes STE as a controlled
language with writing rules and a controlled dictionary.

See [ASD_STE100_REVIEW.md](ASD_STE100_REVIEW.md) for the detailed review and
the decision record.
