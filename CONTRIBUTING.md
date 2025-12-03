The XRP Ledger has many and diverse stakeholders, and everyone deserves
a chance to contribute meaningful changes to the code that runs the
XRPL.

# Contributing

We assume you are familiar with the general practice of [making
contributions on GitHub][contrib]. This file includes only special
instructions specific to this project.

## Before you start

The following branches exist in the main project repository:

- `develop`: The latest set of unreleased features, and the most common
  starting point for contributions.
- `release`: The latest beta release or release candidate.
- `master`: The latest stable release.
- `gh-pages`: The documentation for this project, built by Doxygen.

The tip of each branch must be signed. In order for GitHub to sign a
squashed commit that it builds from your pull request, GitHub must know
your verifying key. Please set up [signature verification][signing].

In general, external contributions should be developed in your personal
[fork][forking]. Contributions from developers with write permissions
should be done in [the main repository][rippled] in a branch with
a permitted prefix. Permitted prefixes are:

- XLS-[a-zA-Z0-9]+/.+
  - e.g. XLS-0033d/mpt-clarify-STEitherAmount
- [GitHub username]/.+
  - e.g. JoelKatz/fix-rpc-webhook-queue
- [Organization name]/.+
  - e.g. ripple/antithesis

Regardless of where the branch is created, please open a _draft_ pull
request as soon as possible after pushing the branch to Github, to
increase visibility, and ease feedback during the development process.

## Major contributions

If your contribution is a major feature or breaking change, then you
must first write an XRP Ledger Standard (XLS) describing it. Go to
[XRPL-Standards](https://github.com/XRPLF/XRPL-Standards/discussions),
choose the next available standard number, and open a discussion with an
appropriate title to propose your draft standard.

When you submit a pull request, please link the corresponding XLS in the
description. An XLS still in `Draft` status is considered a
work-in-progress and open for discussion. Please allow time for
questions, suggestions, and changes to the XLS draft. It is the
responsibility of the XLS author to update the draft to match the final
implementation when its corresponding pull request is merged, unless the
author delegates that responsibility to others.

Any amendment or major RPC change requires either a new XLS or an update
to an existing XLS. Neither change will be released (in an amendment's
case, marked as `Supported::yes`) until the corresponding XLS's status
is `Final`.

## Before making a pull request

(Or marking a draft pull request as ready.)

Changes that alter transaction processing must be guarded by an
[Amendment](https://xrpl.org/amendments.html).
All other changes that maintain the existing behavior do not need an
Amendment.

Ensure that your code compiles according to the build instructions in
[`BUILD.md`](./BUILD.md).

Please write tests for your code.
If your test can be run offline, in under 60 seconds, then it can be an
automatic test run by `rippled --unittest`.
Otherwise, it must be a manual test.

If you create new source files, they must be organized as follows:

- If the files are in any of the `libxrpl` modules, the headers (`.h`) must go
  under `include/xrpl`, and source (`.cpp`) files must go under
  `src/libxrpl`.
- All other non-test files must go under `src/xrpld`.
- All test source files must go under `src/test`.

The source must be formatted according to the style guide below.

Header includes must be [levelized](.github/scripts/levelization).

Changes should be usually squashed down into a single commit.
Some larger or more complicated change sets make more sense,
and are easier to review if organized into multiple logical commits.
Either way, all commits should fit the following criteria:

- Changes should be presented in a single commit or a logical
  sequence of commits.
  Specifically, chronological commits that simply
  reflect the history of how the author implemented
  the change, "warts and all", are not useful to
  reviewers.
- Every commit should have a [good message](#good-commit-messages).
  to explain a specific aspects of the change.
- Every commit should be signed.
- Every commit should be well-formed (builds successfully,
  unit tests passing), as this helps to resolve merge
  conflicts, and makes it easier to use `git bisect`
  to find bugs.

### Good commit messages

Refer to
["How to Write a Git Commit Message"](https://cbea.ms/git-commit/)
for general rules on writing a good commit message.

tl;dr

> 1. Separate subject from body with a blank line.
> 2. Limit the subject line to 50 characters.
>    - [...]shoot for 50 characters, but consider 72 the hard limit.
> 3. Capitalize the subject line.
> 4. Do not end the subject line with a period.
> 5. Use the imperative mood in the subject line.
>    - A properly formed Git commit subject line should always be able
>      to complete the following sentence: "If applied, this commit will
>      _your subject line here_".
> 6. Wrap the body at 72 characters.
> 7. Use the body to explain what and why vs. how.

In addition to those guidelines, please add one of the following
prefixes to the subject line if appropriate.

- `fix:` - The primary purpose is to fix an existing bug.
- `perf:` - The primary purpose is performance improvements.
- `refactor:` - The changes refactor code without affecting
  functionality.
- `test:` - The changes _only_ affect unit tests.
- `docs:` - The changes _only_ affect documentation. This can
  include code comments in addition to `.md` files like this one.
- `build:` - The changes _only_ affect the build process,
  including CMake and/or Conan settings.
- `chore:` - Other tasks that don't affect the binary, but don't fit
  any of the other cases. e.g. formatting, git settings, updating
  Github Actions jobs.

Whenever possible, when updating commits after the PR is open, please
add the PR number to the end of the subject line. e.g. `test: Add
unit tests for Feature X (#1234)`.

## Pull requests

In general, pull requests use `develop` as the base branch.
The exceptions are

- Fixes and improvements to a release candidate use `release` as the
  base.
- Hotfixes use `master` as the base.

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

Pull requests must be merged by [squash-and-merge][squash]
to preserve a linear history for the `develop` branch.

### "Ready to merge"

A pull request should only have the "Ready to merge" label added when it
meets a few criteria:

1. It must have two approving reviews [as described
   above](#pull-requests). (Exception: PRs that are deemed "trivial"
   only need one approval.)
2. All CI checks must be complete and passed. (One-off failures may
   be acceptable if they are related to a known issue.)
3. The PR must have a [good commit message](#good-commit-messages).
   - If the PR started with a good commit message, and it doesn't
     need to be updated, the author can indicate that in a comment.
   - Any contributor, preferably the author, can leave a comment
     suggesting a commit message.
   - If the author squashes and rebases the code in preparation for
     merge, they should also ensure the commit message(s) are updated
     as well.
4. The PR branch must be up to date with the base branch (usually
   `develop`). This is usually accomplished by merging the base branch
   into the feature branch, but if the other criteria are met, the
   changes can be squashed and rebased on top of the base branch.
5. Finally, and most importantly, the author of the PR must
   positively indicate that the PR is ready to merge. That can be
   accomplished by adding the "Ready to merge" label if their role
   allows, or by leaving a comment to the effect that the PR is ready to
   merge.

Once the "Ready to merge" label is added, a maintainer may merge the PR
at any time, so don't use it lightly.

# Style guide

This is a non-exhaustive list of recommended style guidelines. These are
not always strictly enforced and serve as a way to keep the codebase
coherent rather than a set of _thou shalt not_ commandments.

## Formatting

All code must conform to `clang-format` version 18,
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
The contracts are expressed as `XRPL_ASSERT` or `UNREACHABLE` (defined in
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

- The locations where `assert` or `assert(false)` contracts should continue to be used:
  - `constexpr` functions
  - unit tests i.e. files under `src/test`
  - unit tests-related modules (files under `beast/test` and `beast/unit_test`)
- Outside of the listed locations, do not use `assert`; use `XRPL_ASSERT` instead,
  giving it unique name, with the short description of the contract.
- Outside of the listed locations, do not use `assert(false)`; use
  `UNREACHABLE` instead, giving it unique name, with the description of the
  condition being violated
- The contract name should start with a full name (including scope) of the
  function, optionally a named lambda, followed by a colon `:` and a brief
  (typically at most five words) description. `UNREACHABLE` contracts
  can use slightly longer descriptions. If there are multiple overloads of the
  function, use common sense to balance both brevity and unambiguity of the
  function name. NOTE: the purpose of name is to provide stable means of
  unique identification of every contract; for this reason try to avoid elements
  which can change in some obvious refactors or when reinforcing the condition.
- Contract description typically (except for `UNREACHABLE`) should describe the
  _expected_ condition, as in "I assert that _expected_ is true".
- Contract description for `UNREACHABLE` should describe the _unexpected_
  situation which caused the line to have been reached.
- Example good name for an
  `UNREACHABLE` macro `"Json::operator==(Value, Value) : invalid type"`; example
  good name for an `XRPL_ASSERT` macro `"Json::Value::asCString : valid type"`.
- Example **bad** name
  `"RFC1751::insert(char* s, int x, int start, int length) : length is greater than or equal zero"`
  (missing namespace, unnecessary full function signature, description too verbose).
  Good name: `"ripple::RFC1751::insert : minimum length"`.
- In **few** well-justified cases a non-standard name can be used, in which case a
  comment should be placed to explain the rationale (example in `contract.cpp`)
- Do **not** rename a contract without a good reason (e.g. the name no longer
  reflects the location or the condition being checked)
- Do not use `std::unreachable`
- Do not put contracts where they can be violated by an external condition
  (e.g. timing, data payload before mandatory validation etc.) as this creates
  bogus bug reports (and causes crashes of Debug builds)

## Unit Tests

To execute all unit tests:

`rippled --unittest --unittest-jobs=<number of cores>`

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

Maintainers are users with maintain or admin access to the repo.

- [bthomee](https://github.com/bthomee) (Ripple)
- [intelliot](https://github.com/intelliot) (Ripple)
- [JoelKatz](https://github.com/JoelKatz) (Ripple)
- [legleux](https://github.com/legleux) (Ripple)
- [mankins](https://github.com/mankins) (XRP Ledger Foundation)
- [WietseWind](https://github.com/WietseWind) (XRPL Labs + XRP Ledger Foundation)
- [ximinez](https://github.com/ximinez) (Ripple)

## Current Code Reviewers

Code Reviewers are developers who have the ability to review, approve, and
in some cases merge source code changes.

- [a1q123456](https://github.com/a1q123456) (Ripple)
- [Bronek](https://github.com/Bronek) (Ripple)
- [bthomee](https://github.com/bthomee) (Ripple)
- [ckeshava](https://github.com/ckeshava) (Ripple)
- [dangell7](https://github.com/dangell7) (XRPL Labs)
- [godexsoft](https://github.com/godexsoft) (Ripple)
- [gregtatcam](https://github.com/gregtatcam) (Ripple)
- [kuznetsss](https://github.com/kuznetsss) (Ripple)
- [lmaisons](https://github.com/lmaisons) (Ripple)
- [mathbunnyru](https://github.com/mathbunnyru) (Ripple)
- [mvadari](https://github.com/mvadari) (Ripple)
- [oleks-rip](https://github.com/oleks-rip) (Ripple)
- [PeterChen13579](https://github.com/PeterChen13579) (Ripple)
- [pwang200](https://github.com/pwang200) (Ripple)
- [q73zhao](https://github.com/q73zhao) (Ripple)
- [shawnxie999](https://github.com/shawnxie999) (Ripple)
- [Tapanito](https://github.com/Tapanito) (Ripple)
- [ximinez](https://github.com/ximinez) (Ripple)

Developers not on this list are able and encouraged to submit feedback
on pending code changes (open pull requests).

## Instructions for maintainers

These instructions assume you have your git upstream remotes configured
to avoid accidental pushes to the main repo, and a remote group
specifying both of them. e.g.

```
$ git remote -v | grep upstream
upstream        https://github.com/XRPLF/rippled.git (fetch)
upstream        https://github.com/XRPLF/rippled.git (push)
upstream-push   git@github.com:XRPLF/rippled.git (fetch)
upstream-push   git@github.com:XRPLF/rippled.git (push)

$ git config remotes.upstreams
upstream upstream-push
```

You can use the [setup-upstreams] script to set this up.

It also assumes you have a default gpg signing key set up in git. e.g.

```
$ git config user.signingkey
968479A1AFF927E37D1A566BB5690EEEBB952194
# (This is github's key. Use your own.)
```

### When and how to merge pull requests

The maintainer should double-check that the PR has met all the
necessary criteria, and can request additional information from the
owner, or additional reviews, and can always feel free to remove the
"Ready to merge" label if appropriate. The maintainer has final say on
whether a PR gets merged, and are encouraged to communicate and issues
or concerns to other maintainers.

#### Most pull requests: "Squash and merge"

Most pull requests don't need special handling, and can simply be
merged using the "Squash and merge" button on the Github UI. Update
the suggested commit message, or modify it as needed.

#### Slightly more complicated pull requests

Some pull requests need to be pushed to `develop` as more than one
commit. A PR author may _request_ to merge as separate commits. They
must _justify_ why separate commits are needed, and _specify_ how they
would like the commits to be merged. If you disagree with the author,
discuss it with them directly.

If the process is reasonable, follow it. The simplest option is to do a
fast forward only merge (`--ff-only`) on the command line and push to
`develop`.

Some examples of when separate commits are worthwhile are:

1. PRs where source files are reorganized in multiple steps.
2. PRs where the commits are mostly independent and _could_ be separate
   PRs, but are pulled together into one PR under a commit theme or
   issue.
3. PRs that are complicated enough that `git bisect` would not be much
   help if it determined this PR introduced a problem.

Either way, check that:

- The commits are based on the current tip of `develop`.
- The commits are clean: No merge commits (except when reverse
  merging), no "[FOLD]" or "fixup!" messages.
- All commits are signed. If the commits are not signed by the author, use
  `git commit --amend -S` to sign them yourself.
- At least one (but preferably all) of the commits has the PR number
  in the commit message.

The "Create a merge commit" and "Rebase and merge" options should be
disabled in the Github UI, but if you ever find them available **Do not
use them!**

### Releases

All releases, including release candidates and betas, are handled
differently from typical PRs. Most importantly, never use
the Github UI to merge a release.

Rippled uses a linear workflow model that can be summarized as:

1. In between releases, developers work against the `develop` branch.
2. Periodically, a maintainer will build and tag a beta version from
   `develop`, which is pushed to `release`.
   - Betas are usually released every two to three weeks, though that
     schedule can vary depending on progress, availability, and other
     factors.
3. When the changes in `develop` are considered stable and mature enough
   to be ready to release, a release candidate (RC) is built and tagged
   from `develop`, and merged to `release`.
   - Further development for that release (primarily fixes) then
     continues against `release`, while other development continues on
     `develop`. Effectively, `release` is forked from `develop`. Changes
     to `release` must be reverse merged to `develop`.
4. When the candidate has passed testing and is ready for release, the
   final release is merged to `master`.
5. If any issues are found post-release, a hotfix / point release may be
   created, which is merged to `master`, and then reverse merged to
   `develop`.

#### Betas, and the first release candidate

##### Preparing the `develop` branch

1. Optimally, the `develop` branch will be ready to go, with all
   relevant PRs already merged.
2. If there are any PRs pending, merge them **BEFORE** preparing the beta.
   1. If only one or two PRs need to be merged, merge those PRs [as
      normal](#when-and-how-to-merge-pull-requests), updating the second
      one, and waiting for CI to finish in between.
   2. If there are several pending PRs, do not use the Github UI,
      because the delays waiting for CI in between each merge will be
      unnecessarily onerous. (Incidentally, this process can also be
      used to merge if the Github UI has issues.) Merge each PR branch
      directly to a `release-next` on your local machine and create a single
      PR, then push your branch to `develop`.
      1. Squash the changes from each PR, one commit each (unless more
         are needed), being sure to sign each commit and update the
         commit message to include the PR number. You may be able to use
         a fast-forward merge for the first PR.
      2. Push your branch.
      3. Continue to [Making the release](#making-the-release) to update
         the version number, etc.

      The workflow may look something like:

```
git fetch --multiple upstreams user1 user2 user3 [...]
git checkout -B release-next --no-track upstream/develop

# Only do an ff-only merge if prbranch1 is either already
# squashed, or needs to be merged with separate commits,
# and has no merge commits.
# Use -S on the ff-only merge if prbranch1 isn't signed.
git merge [-S] --ff-only user1/prbranch1

git merge --squash user2/prbranch2
git commit -S # Use the commit message provided on the PR

git merge --squash user3/prbranch3
git commit -S # Use the commit message provided on the PR

[...]

# Make sure the commits look right
git log --show-signature "upstream/develop..HEAD"

git push --set-upstream origin

# Continue to "Making the release" to update the version number, so
# everything can be done in one PR.
```

You can also use the [squash-branches] script.

You may also need to manually close the open PRs after the changes are
merged to `develop`. Be sure to include the commit ID.

##### Making the release

This includes, betas, and the first release candidate (RC).

1. If you didn't create one [preparing the `develop`
   branch](#preparing-the-develop-branch), Ensure there is no old
   `release-next` branch hanging around. Then make a `release-next`
   branch that only changes the version number. e.g.

```
git fetch upstreams

git checkout --no-track -B release-next upstream/develop

v="A.B.C-bD"
build=$( find -name BuildInfo.cpp )
sed 's/\(^.*versionString =\).*$/\1 "'${v}'"/' ${build} > version.cpp && mv -vi version.cpp ${build}

git diff

git add ${build}

git commit -S -m "Set version to ${v}"

# You could use your "origin" repo, but some CI tests work better on upstream.
git push upstream-push
git fetch upstreams
git branch --set-upstream-to=upstream/release-next
```

You can also use the [update-version] script. 2. Create a Pull Request for `release-next` with **`develop`** as
the base branch.

1.  Use the title "[TRIVIAL] Set version to X.X.X-bX".
2.  Instead of the default description template, use the following:

```
## High Level Overview of Change

This PR only changes the version number. It will be merged as
soon as Github CI actions successfully complete.
```

3. Wait for CI to successfully complete, and get someone to approve
   the PR. (It is safe to ignore known CI issues.)
4. Push the updated `develop` branch using your `release-next`
   branch. **Do not use the Github UI. It's important to preserve
   commit IDs.**

```
git push upstream-push release-next:develop
```

5. In the unlikely event that the push fails because someone has merged
   something else in the meantime, rebase your branch onto the updated
   `develop` branch, push again, and go back to step 3.
6. Ensure that your PR against `develop` is closed. Github should do it
   automatically.
7. Once this is done, forward progress on `develop` can continue
   (other PRs may be merged).
8. Now create a Pull Request for `release-next` with **`release`** as
   the base branch. Instead of the default template, reuse and update
   the message from the previous release. Include the following verbiage
   somewhere in the description:

```
The base branch is `release`. [All releases (including
betas)](https://github.com/XRPLF/rippled/blob/develop/CONTRIBUTING.md#before-you-start)
go in `release`. This PR branch will be pushed directly to `release` (not
squashed or rebased, and not using the GitHub UI).
```

7. Sign-offs for the three platforms (Linux, Mac, Windows) usually occur
   offline, but at least one approval will be needed on the PR.
   - If issues are discovered during testing, simply abandon the
     release. It's easy to start a new release, it should be easy to
     abandon one. **DO NOT REUSE THE VERSION NUMBER.** e.g. If you
     abandon 2.4.0-b1, the next attempt will be 2.4.0-b2.
8. Once everything is ready to go, push to `release`.

```
git fetch upstreams

# Just to be safe, do a dry run first:
git push --dry-run upstream-push release-next:release

# If everything looks right, push the branch
git push upstream-push release-next:release

# Check that all of the branches are updated
git fetch upstreams
git log -1 --oneline
# The output should look like:
# 0123456789 (HEAD -> upstream/release-next, upstream/release,
#            upstream/develop) Set version to 2.4.0-b1
# Note that upstream/develop may not be on this commit, but
# upstream/release must be.
# Other branches, including some from upstream-push, may also be
# present.
```

9. Tag the release, too.

```
git tag <version number>
git push upstream-push <version number>
```

10. Delete the `release-next` branch on the repo. Use the Github UI or:

```
git push --delete upstream-push release-next
```

11. Finally [create a new release on
    Github](https://github.com/XRPLF/rippled/releases).

#### Release candidates after the first

Once the first release candidate is [merged into
release](#making-the-release), then `release` and `develop` _are allowed
to diverge_.

If a bug or issue is discovered in a version that has a release
candidate being tested, any fix and new version will need to be applied
against `release`, then reverse-merged to `develop`. This helps keep git
history as linear as possible.

A `release-next` branch will be created from `release`, and any further
work for that release must be based on `release-next`. Specifically,
PRs must use `release-next` as the base, and those PRs will be merged
directly to `release-next` when approved. Changes should be restricted
to bug fixes, but other changes may be necessary from time to time.

1. Open any PRs for the pending release using `release-next` as the base,
   so they can be merged directly in to it. Unlike `develop`, though,
   `release-next` can be thrown away and recreated if necessary.
2. Once a new release candidate is ready, create a version commit as in
   step 1 [above](#making-the-release) on `release-next`. You can use
   the [update-version] script for this, too.
3. Jump to step 8 ("Now create a Pull Request for `release-next` with
   **`release`** as the base") from the process
   [above](#making-the-release) to merge `release-next` into `release`.

##### Follow up: reverse merge

Once the RC is merged and tagged, it needs to be reverse merged into
`develop` as soon as possible.

1. Create a branch, based on `upstream/develop`.
   The branch name is not important, but could include "mergeNNNrcN".
   E.g. For release A.B.C-rcD, use `mergeABCrcD`.

```
git fetch upstreams

git checkout --no-track -b mergeABCrcD upstream/develop
```

2. Merge `release` into your branch.

```
# I like the "--edit --log --verbose" parameters, but they are
# not required.
git merge upstream/release
```

3. `BuildInfo.cpp` will have a conflict with the version number.
   Resolve it with the version from `develop` - the higher version.
4. Push your branch to your repo (or `upstream` if you have permission),
   and open a normal PR against `develop`. The "High level overview" can
   simply indicate that this is a merge of the RC. The "Context" should
   summarize the changes from the RC. Include the following text
   prominently:

```
This PR must be merged manually using a push. Do not use the Github UI.
```

5. Depending on the complexity of the changes, and/or merge conflicts,
   the PR may need a thorough review, or just a sign-off that the
   merge was done correctly.
6. If `develop` is updated before this PR is merged, do not merge
   `develop` back into your branch. Instead rebase preserving merges,
   or do the merge again. (See also the `rerere` git config setting.)

```
git rebase --rebase-merges upstream/develop
# OR
git reset --hard upstream/develop
git merge upstream/release
```

7. When the PR is ready, push it to `develop`.

```
git fetch upstreams

# Make sure the commits look right
git log --show-signature "upstream/develop^..HEAD"

git push upstream-push mergeABCrcD:develop

git fetch upstreams
```

Development on `develop` can proceed as normal.

#### Final releases

A final release is any release that is not a beta or RC, such as 2.2.0.

Only code that has already been tested and vetted across all three
platforms should be included in a final release. Most of the time, that
means that the commit immediately preceding the commit setting the
version number will be an RC. Occasionally, there may be last-minute bug
fixes included as well. If so, those bug fixes must have been tested
internally as if they were RCs (at minimum, ensuring unit tests pass,
and the app starts, syncs, and stops cleanly across all three
platforms.)

_If in doubt, make an RC first._

The process for building a final release is very similar to [the process
for building a beta](#making-the-release), except the code will be
moving from `release` to `master` instead of from `develop` to
`release`, and both branches will be pushed at the same time.

1. Ensure there is no old `master-next` branch hanging around.
   Then make a `master-next` branch that only changes the version
   number. As above, or using the
   [update-version] script.
2. Create a Pull Request for `master-next` with **`master`** as
   the base branch. Instead of the default template, reuse and update
   the message from the previous final release. Include the following verbiage
   somewhere in the description:

```
The base branch is `master`. This PR branch will be pushed directly to
`release` and `master` (not squashed or rebased, and not using the
GitHub UI).
```

7. Sign-offs for the three platforms (Linux, Mac, Windows) usually occur
   offline, but at least one approval will be needed on the PR.
   - If issues are discovered during testing, close the PR, delete
     `master-next`, and move development back to `release`, [issuing
     more RCs as necessary](#release-candidates-after-the-first)
8. Once everything is ready to go, push to `release` and `master`.

```
git fetch upstreams

# Just to be safe, do dry runs first:
git push --dry-run upstream-push master-next:release
git push --dry-run upstream-push master-next:master

# If everything looks right, push the branch
git push upstream-push master-next:release
git push upstream-push master-next:master

# Check that all of the branches are updated
git fetch upstreams
git log -1 --oneline
# The output should look like:
# 0123456789 (HEAD -> upstream/master-next, upstream/master,
#            upstream/release) Set version to A.B.0
# Note that both upstream/release and upstream/master must be on this
# commit.
# Other branches, including some from upstream-push, may also be
# present.
```

9. Tag the release, too.

```
git tag <version number>
git push upstream-push <version number>
```

10. Delete the `master-next` branch on the repo. Use the Github UI or:

```
git push --delete upstream-push master-next
```

11. [Create a new release on
    Github](https://github.com/XRPLF/rippled/releases). Be sure that
    "Set as the latest release" is checked.
12. Finally [reverse merge the release into `develop`](#follow-up-reverse-merge).

#### Special cases: point releases, hotfixes, etc.

On occassion, a bug or issue is discovered in a version that already
had a final release. Most of the time, development will have started
on the next version, and will usually have changes in `develop`
and often in `release`.

Because git history is kept as linear as possible, any fix and new
version will need to be applied against `master`.

The process for building a hotfix release is very similar to [the
process for building release candidates after the
first](#release-candidates-after-the-first) and [for building a final
release](#final-releases), except the changes will be done against
`master` instead of `release`.

If there is only a single issue for the hotfix, the work can be done in
any branch. When it's ready to merge, jump to step 3 using your branch
instead of `master-next`.

1. Create a `master-next` branch from `master`.

```
git checkout --no-track -b master-next upstream/master
git push upstream-push
git fetch upstreams
```

2. Open any PRs for the pending hotfix using `master-next` as the base,
   so they can be merged directly in to it. Unlike `develop`, though,
   `master-next` can be thrown away and recreated if necessary.
3. Once the hotfix is ready, create a version commit using the same
   steps as above, or use the
   [update-version] script.
4. Create a Pull Request for `master-next` with **`master`** as
   the base branch. Instead of the default template, reuse and update
   the message from the previous final release. Include the following verbiage
   somewhere in the description:

```
The base branch is `master`. This PR branch will be pushed directly to
`master` (not squashed or rebased, and not using the GitHub UI).
```

7. Sign-offs for the three platforms (Linux, Mac, Windows) usually occur
   offline, but at least one approval will be needed on the PR.
   - If issues are discovered during testing, update `master-next` as
     needed, but ensure that the changes are properly squashed, and the
     version setting commit remains last
8. Once everything is ready to go, push to `master` **only**.

```
git fetch upstreams

# Just to be safe, do a dry run first:
git push --dry-run upstream-push master-next:master

# If everything looks right, push the branch
git push upstream-push master-next:master

# Check that all of the branches are updated
git fetch upstreams
git log -1 --oneline
# The output should look like:
# 0123456789 (HEAD -> upstream/master-next, upstream/master) Set version
#            to 2.4.1
# Note that upstream/master must be on this commit. upstream/release and
# upstream/develop should not.
# Other branches, including some from upstream-push, may also be
# present.
```

9. Tag the release, too.

```
git tag <version number>
git push upstream-push <version number>
```

9. Delete the `master-next` branch on the repo.

```
git push --delete upstream-push master-next
```

10. [Create a new release on
    Github](https://github.com/XRPLF/rippled/releases). Be sure that
    "Set as the latest release" is checked.

Once the hotfix is released, it needs to be reverse merged into
`develop` as soon as possible. It may also need to be merged into
`release` if a release candidate is under development.

1. Create a branch in your own repo, based on `upstream/develop`.
   The branch name is not important, but could include "mergeNNN".
   E.g. For release 2.2.3, use `merge223`.

```
git fetch upstreams

git checkout --no-track -b merge223 upstream/develop
```

2. Merge master into your branch.

```
# I like the "--edit --log --verbose" parameters, but they are
# not required.
git merge upstream/master
```

3. `BuildInfo.cpp` will have a conflict with the version number.
   Resolve it with the version from `develop` - the higher version.
4. Push your branch to your repo, and open a normal PR against
   `develop`. The "High level overview" can simply indicate that this
   is a merge of the hotfix version. The "Context" should summarize
   the changes from the hotfix. Include the following text
   prominently:

```
This PR must be merged manually using a --ff-only merge. Do not use the Github UI.
```

5. Depending on the complexity of the hotfix, and/or merge conflicts,
   the PR may need a thorough review, or just a sign-off that the
   merge was done correctly.
6. If `develop` is updated before this PR is merged, do not merge
   `develop` back into your branch. Instead rebase preserving merges,
   or do the merge again. (See also the `rerere` git config setting.)

```
git rebase --rebase-merges upstream/develop
# OR
git reset --hard upstream/develop
git merge upstream/master
```

7. When the PR is ready, push it to `develop`.

```
git fetch upstreams

# Make sure the commits look right
git log --show-signature "upstream/develop..HEAD"

git push upstream-push HEAD:develop
```

Development on `develop` can proceed as normal. It is recommended to
create a beta (or RC) immediately to ensure that everything worked as
expected.

##### An even rarer scenario: A hotfix on an old release

Historically, once a final release is tagged and packages are released,
versions older than the latest final release are no longer supported.
However, there is a possibility that a very high severity bug may occur
in a non-amendment blocked version that is still being run by
a significant fraction of users, which would necessitate a hotfix / point
release to that version as well as any later versions.

This scenario would follow the same basic procedure as above,
except that _none_ of `develop`, `release`, or `master`
would be touched during the release process.

In this example, consider if version 2.1.1 needed to be patched.

1. Create two branches in the main (`upstream`) repo.

```
git fetch upstreams

# Create a base branch off the tag
git checkout --no-track -b master-2.1.2 2.1.1
git push upstream-push

# Create a working branch
git checkout --no-track -b master212-next master-2.1.2
git push upstream-push

git fetch upstreams
```

2. Work continues as above, except using `master-2.1.2`as
   the base branch for any merging, packaging, etc.
3. After the release is tagged and packages are built, you could
   potentially delete both branches, e.g. `master-2.1.2` and
   `master212-next`. However, it may be useful to keep `master-2.1.2`
   around indefinitely for reference.
4. Assuming that a hotfix is also released for the latest
   version in parallel with this one, or if the issue is
   already fixed in the latest version, do no do any
   reverse merges. However, if it is not, it probably makes
   sense to reverse merge `master-2.1.2` into `master`,
   release a hotfix for _that_ version, then reverse merge
   from `master` to `develop`. (Please don't do this unless absolutely
   necessary.)

[contrib]: https://docs.github.com/en/get-started/quickstart/contributing-to-projects
[squash]: https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/incorporating-changes-from-a-pull-request/about-pull-request-merges#squash-and-merge-your-commits
[forking]: https://github.com/XRPLF/rippled/fork
[rippled]: https://github.com/XRPLF/rippled
[signing]: https://docs.github.com/en/authentication/managing-commit-signature-verification/about-commit-signature-verification
[setup-upstreams]: ./bin/git/setup-upstreams.sh
[squash-branches]: ./bin/git/squash-branches.sh
[update-version]: ./bin/git/update-version.sh
