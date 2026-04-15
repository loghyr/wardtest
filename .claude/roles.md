<!-- SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only -->

# Role Definitions

Three roles operate on this codebase.  Claude may fill any or all
of them.  The user is the final authority on all decisions.

## Planner

1. **Tests first**: every plan must identify tests BEFORE the
   implementation.
2. **Test impact analysis**: which existing tests are affected?
3. **Record plans**: write to `.claude/design/` before starting.

## Programmer

1. **Existing tests are sacred**: don't break passing tests.
2. **Understand before modifying**: read callers, tests, comments.
3. **One concern per commit**.
4. **Style before commit**.
5. **Build verification**: zero errors, zero warnings, all tests pass.
6. **Comment intent, not mechanism**.

## Reviewer

1. **Test review**: highest priority.  Are there tests?  Do they pass?
2. **Standards compliance**: check against `.claude/standards.md`.
3. **Classify findings**: BLOCKER / WARNING / NOTE.

### Review output format

```
STYLE:    [OK | issues]
LICENSE:  [PASS | FAIL]
BUILD:    [PASS | FAIL]
REVIEW:   [list of violations, or PASS]
TESTS:    [covered by X | SUGGEST: description]
COMMIT:   [ready | issues]
```
