The XRP Ledger has many and diverse stakeholders, and everyone deserves
a chance to contribute meaningful changes to the code that runs the
XRPL.

# Contributing

We assume you are familiar with the general practice of [making
contributions on GitHub][1]. This file includes only special
instructions specific to this project.


## Before you start

In general, contributions should be developed in your personal
[fork](https://github.com/XRPLF/rippled/fork).

The following branches exist in the main project repository:

- `develop`: The latest set of unreleased features, and the most common
    starting point for contributions.
- `release`: The latest beta release or release candidate.
- `master`: The latest stable release.
- `gh-pages`: The documentation for this project, built by Doxygen.

The tip of each branch must be signed. In order for GitHub to sign a
squashed commit that it builds from your pull request, GitHub must know
your verifying key. Please set up [signature verification][signing].

[rippled]: https://github.com/XRPLF/rippled
[signing]:
    https://docs.github.com/en/authentication/managing-commit-signature-verification/about-commit-signature-verification


## Major contributions

If your contribution is a major feature or breaking change, then you
must first write an XRP Ledger Standard (XLS) describing it. Go to
[XRPL-Standards](https://github.com/XRPLF/XRPL-Standards/discussions),
choose the next available standard number, and open a discussion with an
appropriate title to propose your draft standard.

When you submit a pull request, please link the corresponding XLS in the
description. An XLS still in draft status is considered a
work-in-progress and open for discussion. Please allow time for
questions, suggestions, and changes to the XLS draft. It is the
responsibility of the XLS author to update the draft to match the final
implementation when its corresponding pull request is merged, unless the
author delegates that responsibility to others.


## Before making a pull request

Changes that alter transaction processing must be guarded by an
[Amendment](https://xrpl.org/amendments.html).
All other changes that maintain the existing behavior do not need an
Amendment.

Ensure that your code compiles according to the build instructions in
[`BUILD.md`](./BUILD.md).
If you create new source files, they must go under `src/ripple`.
You will need to add them to one of the
[source lists](./Builds/CMake/RippledCore.cmake) in CMake.

Please write tests for your code.
If you create new test source files, they must go under `src/test`.
You will need to add them to one of the
[source lists](./Builds/CMake/RippledCore.cmake) in CMake.
If your test can be run offline, in under 60 seconds, then it can be an
automatic test run by `rippled --unittest`.
Otherwise, it must be a manual test.

The source must be formatted according to the style guide below.

Header includes must be [levelized](./Builds/levelization).


## Pull requests

In general, pull requests use `develop` as the base branch.

(Hotfixes are an exception.)

Changes to pull requests must be added as new commits.
Once code reviewers have started looking at your code, please avoid
force-pushing a branch in a pull request.
This preserves the ability for reviewers to filter changes since their last
review.

A pull request must obtain **approvals from at least two reviewers** before it
can be considered for merge by a Maintainer.
Maintainers retain discretion to require more approvals if they feel the
credibility of the existing approvals is insufficient.

Pull requests must be merged by [squash-and-merge][2]
to preserve a linear history for the `develop` branch.


# Style guide

This is a non-exhaustive list of recommended style guidelines. These are
not always strictly enforced and serve as a way to keep the codebase
coherent rather than a set of _thou shalt not_ commandments.


## Formatting

All code must conform to `clang-format` version 10,
according to the settings in [`.clang-format`](./.clang-format),
unless the result would be unreasonably difficult to read or maintain.
To demarcate lines that should be left as-is, surround them with comments like
this:

```
// clang-format off
...
// clang-format on
```

You can format individual files in place by running `clang-format -i <file>...`
from any directory within this project.

You can install a pre-commit hook to automatically run `clang-format` before every commit:
```
pip3 install pre-commit
pre-commit install
```

## Unit Tests
To execute all unit tests:

```rippled --unittest --unittest-jobs=<number of cores>```

(Note: Using multiple cores on a Mac M1 can cause spurious test failures. The 
cause is still under investigation. If you observe this problem, try specifying fewer jobs.)

To run a specific set of test suites:

```
rippled --unittest TestSuiteName
```
Note: In this example, all tests with prefix `TestSuiteName` will be run, so if
`TestSuiteName1` and `TestSuiteName2` both exist, then both tests will run. 
Alternatively, if the unit test name finds an exact match, it will stop 
doing partial matches, i.e. if a unit test with a title of `TestSuiteName` 
exists, then no other unit test will be executed, apart from `TestSuiteName`.

## Avoid

1. Proliferation of nearly identical code.
2. Proliferation of new files and classes.
3. Complex inheritance and complex OOP patterns.
4. Unmanaged memory allocation and raw pointers.
5. Macros and non-trivial templates (unless they add significant value).
6. Lambda patterns (unless these add significant value).
7. CPU or architecture-specific code unless there is a good reason to
   include it, and where it is used, guard it with macros and provide
   explanatory comments.
8. Importing new libraries unless there is a very good reason to do so.


## Seek to

9. Extend functionality of existing code rather than creating new code.
10. Prefer readability over terseness where important logic is
    concerned.
11. Inline functions that are not used or are not likely to be used
    elsewhere in the codebase.
12. Use clear and self-explanatory names for functions, variables,
    structs and classes.
13. Use TitleCase for classes, structs and filenames, camelCase for
    function and variable names, lower case for namespaces and folders.
14. Provide as many comments as you feel that a competent programmer
    would need to understand what your code does.


# Maintainers

Maintainers are ecosystem participants with elevated access to the repository.
They are able to push new code, make decisions on when a release should be
made, etc.


## Adding and removing

New maintainers can be proposed by two existing maintainers, subject to a vote
by a quorum of the existing maintainers.
A minimum of 50% support and a 50% participation is required.
In the event of a tie vote, the addition of the new maintainer will be
rejected.

Existing maintainers can resign, or be subject to a vote for removal at the
behest of two existing maintainers.
A minimum of 60% agreement and 50% participation are required.
The XRP Ledger Foundation will have the ability, for cause, to remove an
existing maintainer without a vote.


## Current Maintainers

Maintainers are users with admin access to the repo. Maintainers do not typically approve or deny pull requests.

* [intelliot](https://github.com/intelliot) (Ripple)
* [JoelKatz](https://github.com/JoelKatz) (Ripple)
* [nixer89](https://github.com/nixer89) (XRP Ledger Foundation)
* [Silkjaer](https://github.com/Silkjaer) (XRP Ledger Foundation)
* [WietseWind](https://github.com/WietseWind) (XRPL Labs + XRP Ledger Foundation)

## Current Code Reviewers

Code Reviewers are developers who have the ability to review and approve source code changes.

* [HowardHinnant](https://github.com/HowardHinnant) (Ripple)
* [scottschurr](https://github.com/scottschurr) (Ripple)
* [seelabs](https://github.com/seelabs) (Ripple)
* [Ed Hennis](https://github.com/ximinez) (Ripple)
* [mvadari](https://github.com/mvadari) (Ripple)
* [thejohnfreeman](https://github.com/thejohnfreeman) (Ripple)
* [Bronek](https://github.com/Bronek) (Ripple)
* [manojsdoshi](https://github.com/manojsdoshi) (Ripple)
* [godexsoft](https://github.com/godexsoft) (Ripple)
* [mDuo13](https://github.com/mDuo13) (Ripple)
* [ckniffen](https://github.com/ckniffen) (Ripple)
* [arihantkothari](https://github.com/arihantkothari) (Ripple)
* [pwang200](https://github.com/pwang200) (Ripple)
* [sophiax851](https://github.com/sophiax851) (Ripple)
* [shawnxie999](https://github.com/shawnxie999) (Ripple)
* [gregtatcam](https://github.com/gregtatcam) (Ripple)
* [mtrippled](https://github.com/mtrippled) (Ripple)
* [ckeshava](https://github.com/ckeshava) (Ripple)
* [nbougalis](https://github.com/nbougalis) None
* [RichardAH](https://github.com/RichardAH) (XRPL Labs + XRP Ledger Foundation)
* [dangell7](https://github.com/dangell7) (XRPL Labs)


[1]: https://docs.github.com/en/get-started/quickstart/contributing-to-projects
[2]: https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/incorporating-changes-from-a-pull-request/about-pull-request-merges#squash-and-merge-your-commits
