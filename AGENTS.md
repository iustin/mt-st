# Instructions for AI agents

This file describes the rules for making changes to mt-st. They apply
to every change, however small.

## Building and testing

- `make` builds `mt` and `stinit`.
- `make check` runs the test suite. It needs
  [shelltestrunner](https://github.com/simonmichael/shelltestrunner)
  (the `shelltest` command).
- `make san-check` runs the test suite under the address, memory and
  undefined behaviour sanitizers, like the CI does. It needs `clang`.

## Rules

1. **Always run the tests, including the sanitizers.** Before
   considering a change done, run both `make check` and
   `make san-check`, and make sure both pass. Don't skip the sanitizer
   run because a change looks trivial; many bugs in this code base
   (buffer overflows, integer overflows) only show up there.

2. **Always add tests.** Every bug fix and every behaviour change needs
   a test that covers it, unless it really can't be tested without
   tape hardware (in which case, say so explicitly).
   - Tests are [shelltest](https://github.com/simonmichael/shelltestrunner)
     files in `tests/*.test`; test input files (e.g. stinit definition
     files) go in `tests/data/`.
   - For a bug fix, check that the new test fails without the fix and
     passes with it.
   - Test both the success and the error paths (expected output and
     exit code).

3. **Always document user-visible changes in `CHANGELOG.md`.** Add an
   entry to the `## Unreleased changes` section at the top (create it if
   it's missing) for anything that matters to end users: bug fixes,
   behaviour changes, new or removed options, documentation fixes.
   - The changelog is a summary for users, not a list of commits:
     describe what changed from the user's point of view, and group
     related changes into a single entry (e.g. several parsing fixes
     become one "many parsing fixes, e.g. ..." entry).
   - Put each entry in the matching sub-section:
     - `### Significant changes`: anything that can affect existing
       setups or scripts (e.g. settings applied differently, something
       that used to be accepted and is now an error). Write each one as
       a paragraph (or a few) starting with a bold sentence summarising
       the change, followed by enough detail for users to know whether
       and how they are affected: what was wrong, what happens now, and
       what they should check.
     - `### Bug fixes` and `### Minor changes` (documentation,
       completion, internal changes such as refactoring, comments or
       CI): short bullet points.
   - Credit the people who made the change by name, in parentheses,
     e.g. `(Kai Mäkisara)`, and follow the style of the existing
     entries.
