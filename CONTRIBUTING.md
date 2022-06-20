# Contributing
The XRP Ledger has many and diverse stakeholders, and everyone deserves a chance to contribute meaningful changes to the code that runs the XRPL.
To contribute, please:
1. Fork the repository under your own user.
2. Create a new branch on which to write your changes. Please note that changes which alter transaction processing must be composed via and guarded using [Amendments](https://xrpl.org/amendments.html). Changes which are _read only_ i.e. RPC, or changes which are only refactors and maintain the existing behaviour do not need to be made through an Amendment.
3. Write and test your code.
4. Ensure that your code compiles with the provided build engine and update the provided build engine as part of your PR where needed and where appropriate.
5. Write test cases for your code and include those in `src/test` such that they are runnable from the command line using `./rippled -u`. (Some changes will not be able to be tested this way.)
6. Ensure your code passes automated checks (e.g. clang-format and levelization.)
7. Squash your commits (i.e. rebase) into as few commits as is reasonable to describe your changes at a high level (typically a single commit for a small change.)
8. Open a PR to the main repository onto the _develop_ branch, and follow the provided template.

# Major Changes
If your code change is a major feature, a breaking change or in some other way makes a significant alteration to the way the XRPL will operate, then you must first write an XLS document (XRP Ledger Standard) describing your change.
To do this:
1. Go to [XLS Standards](https://github.com/XRPLF/XRPL-Standards/discussions).
2. Choose the next available standard number.
3. Open a discussion with the appropriate title to propose your draft standard.
4. Link your XLS in your PR.

# Style guide
This is a non-exhaustive list of recommended style guidelines. These are not always strictly enforced and serve as a way to keep the codebase coherent rather than a set of _thou shalt not_ commandments.

## Formatting
All code must conform to `clang-format` version 10, unless the result would be unreasonably difficult to read or maintain.
To change your code to conform use `clang-format -i <your changed files>`.

## Avoid
1. Proliferation of nearly identical code.
2. Proliferation of new files and classes.
3. Complex inheritance and complex OOP patterns.
4. Unmanaged memory allocation and raw pointers.
5. Macros and non-trivial templates (unless they add significant value.)
6. Lambda patterns (unless these add significant value.)
7. CPU or architecture-specific code unless there is a good reason to include it, and where it is used guard it with macros and provide explanatory comments.
8. Importing new libraries unless there is a very good reason to do so.

## Seek to
9. Extend functionality of existing code rather than creating new code.
10. Prefer readability over terseness where important logic is concerned.
11. Inline functions that are not used or are not likely to be used elsewhere in the codebase.
12. Use clear and self-explanatory names for functions, variables, structs and classes.
13. Use TitleCase for classes, structs and filenames, camelCase for function and variable names, lower case for namespaces and folders.
14. Provide as many comments as you feel that a competent programmer would need to understand what your code does.

# Maintainers
Maintainers are ecosystem participants with elevated access to the repository. They are able to push new code, make decisions on when a release should be made, etc.

## Code Review
New contributors' PRs must be reviewed by at least two of the maintainers. Well established prior contributors can be reviewed by a single maintainer.

## Adding and Removing
New maintainers can be proposed by two existing maintainers, subject to a vote by a quorum of the existing maintainers. A minimum of 50% support and a 50% participation is required. In the event of a tie vote, the addition of the new maintainer will be rejected.

Existing maintainers can resign, or be subject to a vote for removal at the behest of two existing maintainers. A minimum of 60% agreement and 50% participation are required. The XRP Ledger Foundation will have the ability, for cause, to remove an existing maintainer without a vote.

## Existing Maintainers
* [JoelKatz](https://github.com/JoelKatz) (Ripple)
* [Manojsdoshi](https://github.com/manojsdoshi) (Ripple)
* [N3tc4t](https://github.com/n3tc4t) (XRPL Labs)
* [Nikolaos D Bougalis](https://github.com/nbougalis) (Ripple)
* [Nixer89](https://github.com/nixer89) (XRP Ledger Foundation)
* [RichardAH](https://github.com/RichardAH) (XRPL Labs + XRP Ledger Foundation)
* [Seelabs](https://github.com/seelabs) (Ripple)
* [Silkjaer](https://github.com/Silkjaer) (XRP Ledger Foundation)
* [WietseWind](https://github.com/WietseWind) (XRPL Labs + XRP Ledger Foundation)
* [Ximinez](https://github.com/ximinez) (Ripple)
