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

Changes should be usually squashed down into a single commit.
Some larger or more complicated change sets make more sense,
and are easier to review if organized into multiple logical commits.
Either way, all commits should fit the following criteria:
* Changes should be presented in a single commit or a logical
  sequence of commits.
  Specifically, chronological commits that simply
  reflect the history of how the author implemented
  the change, "warts and all", are not useful to
  reviewers.
* Every commit should have a [good message](#good-commit-messages).
  to explain a specific aspects of the change.
* Every commit should be signed.
* Every commit should be well-formed (builds successfully,
  unit tests passing), as this helps to resolve merge
  conflicts, and makes it easier to use `git bisect`
  to find bugs.

### Good commit messages

Refer to
["How to Write a Git Commit Message"](https://cbea.ms/git-commit/)
for general rules on writing a good commit message.

In addition to those guidelines, please add one of the following
prefixes to the subject line if appropriate.
* `fix:` - The primary purpose is to fix an existing bug.
* `perf:` - The primary purpose is performance improvements.
* `refactor:` - The changes refactor code without affecting
  functionality.
* `test:` - The changes _only_ affect unit tests.
* `docs:` - The changes _only_ affect documentation. This can
  include code comments in addition to `.md` files like this one.
* `build:` - The changes _only_ affect the build process,
  including CMake and/or Conan settings.
* `chore:` - Other tasks that don't affect the binary, but don't fit
  any of the other cases. e.g. formatting, git settings, updating
  Github Actions jobs.

Whenever possible, when updating commits after the PR is open, please
add the PR number to the end of the subject line. e.g. `test: Add
unit tests for Feature X (#1234)`.

## Pull requests

In general, pull requests use `develop` as the base branch.
(Hotfixes are an exception.)

If your changes are not quite ready, but you want to make it easily available
for preliminary examination or review, you can create a "Draft" pull request.
While a pull request is marked as a "Draft", you can rebase or reorganize the
commits in the pull request as desired.

Github pull requests are created as "Ready" by default, or you can mark
a "Draft" pull request as "Ready".
Once a pull request is marked as "Ready",
any changes must be added as new commits. Do not
force-push to a branch in a pull request under review.
(This includes rebasing your branch onto the updated base branch.
Use a merge operation, instead or hit the "Update branch" button
at the bottom of the Github PR page.)
This preserves the ability for reviewers to filter changes since their last
review.

A pull request must obtain **approvals from at least two reviewers**
before it can be considered for merge by a Maintainer.
Maintainers retain discretion to require more approvals if they feel the
credibility of the existing approvals is insufficient.

Pull requests must be merged by [squash-and-merge][2]
to preserve a linear history for the `develop` branch.

### When and how to merge pull requests

#### "Passed"

A pull request should only have the "Passed" label added when it
meets a few criteria:

1. It must have two approving reviews [as described
   above](#pull-requests). (Exception: PRs that are deemed "trivial"
   only need one approval.)
2. All CI checks must be complete and passed. (One-off failures may
   be acceptable if they are related to a known issue.)
3. The PR must have a [good commit message](#good-commit-messages).
   * If the PR started with a good commit message, and it doesn't
     need to be updated, the author can indicate that in a comment.
   * Any contributor, preferably the author, can leave a comment
     suggesting a commit message.
   * If the author squashes and rebases the code in preparation for
     merge, they should also ensure the commit message(s) are updated
     as well.
4. The PR branch must be up to date with the base branch (usually
   `develop`). This is usually accomplised by merging the base branch
   into the feature branch, but if the other criteria are met, the
   changes can be squashed and rebased on top of the base branch.
5. Finally, and most importantly, the author of the PR must
   positively indicate that the PR is ready to merge. That can be
   accomplished by adding the "Passed" label if their role allows,
   or by leaving a comment to the effect that the PR is ready to
   merge.

Once the "Passed" label is added, a maintainer may merge the PR at
any time, so don't use it lightly.

#### Instructions for maintainers

The maintainer should double-check that the PR has met all the
necessary criteria, and can request additional information from the
owner, or additional reviews, and can always feel free to remove the
"Passed" label if appropriate. The maintainer has final say on
whether a PR gets merged, and are encouraged to communicate and
issues or concerns to other maintainers.

##### Most pull requests: "Squash and merge"

Most pull requests don't need special handling, and can simply be
merged using the "Squash and merge" button on the Github UI. Update
the suggested commit message if necessary.

##### Slightly more complicated pull requests

Some pull requests need to be pushed to `develop` as more than one
commit. There are multiple ways to accomplish this. If the author
describes a process, and it is reasonable, follow it. Otherwise, do
a fast forward only merge (`--ff-only`) on the command line and push.

Either way, check that:
* The commits are based on the current tip of `develop`.
* The commits are clean: No merge commits (except when reverse
  merging), no "[FOLD]" or "fixup!" messages.
* All commits are signed. If the commits are not signed by the author, use
  `git commit --amend -S` to sign them yourself.
* At least one (but preferably all) of the commits has the PR number
  in the commit message.

**Never use the "Create a merge commit" or "Rebase and merge"
 functions!**

##### Releases, release candidates, and betas

All releases, including release candidates and betas, are handled
differently from typical PRs. Most importantly, never use
the Github UI to merge a release.

1. There are two possible conditions that the `develop` branch will
   be in when preparing a release.
   1. Ready or almost ready to go: There may be one or two PRs that
      need to be merged, but otherwise, the only change needed is to
      update the version number in `BuildInfo.cpp`. In this case,
      merge those PRs as appropriate, updating the second one, and
      waiting for CI to finish in between. Then update
      `BuildInfo.cpp`.
   2. Several pending PRs: In this case, do not use the Github UI,
      because the delays waiting for CI in between each merge will be
      unnecessarily onerous. Instead, create a working branch (e.g.
      `develop-next`) based off of `develop`. Squash the changes
      from each PR onto the branch, one commit each (unless
      more are needed), being sure to sign each commit and update
      the commit message to include the PR number. You may be able
      to use a fast-forward merge for the first PR. The workflow may
      look something like:
```
git fetch upstream
git checkout upstream/develop
git checkout -b develop-next
# Use -S on the ff-only merge if prbranch1 isn't signed.
# Or do another branch first.
git merge --ff-only user1/prbranch1
git merge --squash user2/prbranch2
git commit -S
git merge --squash user3/prbranch3
git commit -S
[...]
git push --set-upstream origin develop-next
</pre>
```
2. Create the Pull Request with `release` as the base branch. If any
   of the included PRs are still open,
   [use closing keywords](https://docs.github.com/articles/closing-issues-using-keywords)
   in the description to ensure they are closed when the code is
   released. e.g. "Closes #1234"
3. Instead of the default template, reuse and update the message from
   the previous release. Include the following verbiage somewhere in
   the description:
```
The base branch is release. All releases (including betas) go in
release. This PR will be merged with --ff-only (not squashed or
rebased, and not using the GitHub UI) to both release and develop.
```
4. Sign-offs for the three platforms usually occur offline, but at
   least one approval will be needed on the PR.
5. Once everything is ready to go, open a terminal, and do the
   fast-forward merges manually. Do not push any branches until you
   verify that all of them update correctly.
```
git fetch upstream
git checkout -b upstream--develop -t upstream/develop || git checkout upstream--develop
git reset --hard upstream/develop
# develop-next must be signed already!
git merge --ff-only origin/develop-next
git checkout -b upstream--release -t upstream/release || git checkout upstream--release
git reset --hard upstream/release
git merge --ff-only origin/develop-next
# Only do these 3 steps if pushing a release. No betas or RCs
git checkout -b upstream--master -t upstream/master || git checkout upstream--master
git reset --hard upstream/master
git merge --ff-only origin/develop-next
# Check that all of the branches are updated
git log -1 --oneline
# The output should look like:
# 02ec8b7962 (HEAD -> upstream--master, origin/develop-next, upstream--release, upstream--develop, develop-next) Set version to 2.2.0-rc1
# Note that all of the upstream--develop/release/master are on this commit.
# (Master will be missing for betas, etc.)
# Just to be safe, do a dry run first:
git push --dry-run upstream-push HEAD:develop
git push --dry-run upstream-push HEAD:release
# git push --dry-run upstream-push HEAD:master
# Now push
git push upstream-push HEAD:develop
git push upstream-push HEAD:release
# git push upstream-push HEAD:master
# Don't forget to tag the release, too.
git tag <version number>
git push upstream-push <version number>
```
6. Finally
[create a new release on Github](https://github.com/XRPLF/rippled/releases).


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

There is a Continuous Integration job that runs clang-format on pull requests. If the code doesn't comply, a patch file that corrects auto-fixable formatting issues is generated.

To download the patch file:

1. Next to `clang-format / check (pull_request) Failing after #s` -> click **Details** to open the details page.
2. Left menu -> click **Summary**
3. Scroll down to near the bottom-right under `Artifacts` -> click **clang-format.patch**
4. Download the zip file and extract it to your local git repository. Run `git apply [patch-file-name]`.
5. Commit and push.

You can install a pre-commit hook to automatically run `clang-format` before every commit:
```
pip3 install pre-commit
pre-commit install
```

## Contracts and instrumentation

We are using [Antithesis](https://antithesis.com/) for continuous fuzzing,
and keep a copy of [Antithesis C++ SDK](https://github.com/antithesishq/antithesis-sdk-cpp/)
in `external/antithesis-sdk`. One of the aims of fuzzing is to identify bugs
by finding external conditions which cause contracts violations inside `rippled`.
The contracts are expressed as `ASSERT` or `UNREACHABLE` (defined in
`include/xrpl/beast/utility/instrumentation.h`), which are effectively (outside
of Antithesis) wrappers for `assert(...)` with added name. The purpose of name
is to provide contracts with stable identity which does not rely on line numbers.

When `rippled` is built with the Antithesis instrumentation enabled
(using `voidstar` CMake option) and ran on the Antithesis platform, the
contracts become
[test properties](https://antithesis.com/docs/using_antithesis/properties.html);
otherwise they are just like a regular `assert`.
To learn more about Antithesis, see
[How Antithesis Works](https://antithesis.com/docs/introduction/how_antithesis_works.html)
and [C++ SDK](https://antithesis.com/docs/using_antithesis/sdk/cpp/overview.html#)

We continue to use the old style `assert` or `assert(false)` in certain
locations, where the reporting of contract violations on the Antithesis
platform is either not possible or not useful.

For this reason:
* The locations where `assert` or `assert(false)` contracts should continue to be used:
  * `constexpr` functions
  * unit tests i.e. files under `src/test`
  * unit tests-related modules (files under `beast/test` and `beast/unit_test`)
* Outside of the listed locations, do not use `assert`; use `ASSERT` instead,
  giving it unique name, with the short description of the contract.
* Outside of the listed locations, do not use `assert(false)`; use
  `UNREACHABLE` instead, giving it unique name, with the description of the
  condition being violated
* The contract name should start with a full name (including scope) of the
  function, optionally a named lambda, followed by a colon ` : ` and a brief
  (typically at most five words) description. `UNREACHABLE` contracts
  can use slightly longer descriptions. If there are multiple overloads of the
  function, use common sense to balance both brevity and unambiguity of the
  function name. NOTE: the purpose of name is to provide stable means of
  unique identification of every contract; for this reason try to avoid elements
  which can change in some obvious refactors or when reinforcing the condition.
* Contract description typically (except for `UNREACHABLE`) should describe the
  _expected_ condition, as in "I assert that _expected_ is true".
* Contract description for `UNREACHABLE` should describe the _unexpected_
  situation which caused the line to have been reached.
* Example good name for an
  `UNREACHABLE` macro `"Json::operator==(Value, Value) : invalid type"`; example
  good name for an `ASSERT` macro `"Json::Value::asCString : valid type"`.
* Example **bad** name
  `"RFC1751::insert(char* s, int x, int start, int length) : length is greater than or equal zero"`
  (missing namespace, unnecessary full function signature, description too verbose).
  Good name: `"ripple::RFC1751::insert : minimum length"`.
* In **few** well-justified cases a non-standard name can be used, in which case a
  comment should be placed to explain the rationale (example in `contract.cpp`)
* Do **not** rename a contract without a good reason (e.g. the name no longer
  reflects the location or the condition being checked)
* Do not use `std::unreachable`
* Do not put contracts where they can be violated by an external condition
  (e.g. timing, data payload before mandatory validation etc.) as this creates
  bogus bug reports (and causes crashes of Debug builds)

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
