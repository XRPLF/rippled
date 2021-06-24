# Github configuration

This file provides a brief explanation of the organization of rippled's
Github configuration for actions, workflows, and templates.

## Templates

The `ISSUE_TEMPLATE` folder holds several files used to improve the
experience of creating an issue.
* `config.yml` configures external links to XRPL resources that are provided
  when a user starts the process of creating a new issue. Currently, that
  includes XRPL documentation and Ripple's bug bounty program.
* `bug_report.md` is the template used to create bug reports.
* `feature_request.md` is the template used to create feature requests.

Additionally, `pull_request_template.md` in this folder is the template
used when opening Pull Requests.

## Workflows

The `workflows` folder holds several files that describe Github Actions
workflows.

### Documentation

`doxygen.yml` builds and [publishes](http://ripple.github.io/rippled/) the
rippled developer documentation using `doxygen` whenever a new commit or
commits are published to the `develop` branch.

### Code formatting

`clang-format.yml` checks that source code is correctly formatted. It runs
all rippled source files through `clang-format`, and fails if any changes
are found. If it fails, it publishes a patch file as an artifact that the
developer can apply to their branch, as well as instructions on how to use
it.

### Levelization checking

`levelization.yml` checks for changes in source code levelization, and
raises an alert if any changes are found (good or bad). See [the levelization
documentation](../Builds/levelization/README.md) for more information.

### Continuous integration

The design of Github Actions allows
[workflows to be re-run](https://docs.github.com/en/actions/managing-workflow-runs/re-running-a-workflow#re-run-a-workflow-using-the-github-ui).
However, the workflow must be run _as a whole and in it's entirety_. If a
single job in the workflow fails, it can not be run by itself. Spurious
failures should be rare, but if they happen, it would be convenient to not
have to run all 45 or so jobs just to retry the one. Thus the continuous
integration tasks were split into seven different workflows: five for Linux
jobs, and one each for MacOS and Windows. This allows a small subset of the
jobs to be re-run if necessary. One file describes one workflow.

The workflows are generally built using some or all of the following stages:

1. Build and/or download common dependencies, and store them in a cache
   intended to be shared by later stages. For example, boost, vcpkg, or the
   external projects required by our `cmake` configuration.
2. Build and test "base" configurations. A base configuration is one that
   builds `rippled` using default settings other than compiler,
   debug/release, and unity/non-unity.
3. Use the cache from the base configuration to build and usually test a
   special case configuration (e.g. Coverage or Reporting mode).
4. Use the `rippled` "base" artifact to run a non-default test scenario (e.g.
   manual tests, or IPv6 tests).

Every build and test job, whether base or special case, will store some of
the intermediate build-results (e.g. NIH cache, `ccache` folders, vcpkg
libraries) in a job-specific cache. Additionally, each build and test job
will upload two artifacts: The cmake logs, and the `rippled` executable built
by the job.

#### Linux

1. `linux-general.yml` stages:
    1. Pre-downloads dependencies into "NIH" ("Not Invented Here") caches.
    2. Builds and tests `rippled` using all of the combinations of the
       following **except** those built in the special case jobs below.
       * gcc-8, gcc-9, clang-8, clang-9, and clang-10
       * Debug and Release
       * unity and non-unity
    3. There is no stage 3 in this workflow.
    4. There is no stage 4 in this workflow.
2. `linux-clang8-debug.yml` stages:
    1. There is no stage 1 in this workflow, but if there is an appropriate
       NIH cache available from `linux-general.yml`, it will be used.
    2. Builds and tests a base `rippled` using
       * clang-8
       * Debug
       * unity
    3. Using the cache from the base stage, builds and tests the following
       special configurations:
       * Reporting
       * Coverage
    4. There is no stage 4 in this workflow.
3. `linux-clang8-release.yml` stages:
    1. There is no stage 1 in this workflow, but if there is an appropriate
       NIH cache available from `linux-general.yml`, it will be used.
    2. Builds and tests a base `rippled` using
       * clang-8
       * Release
       * unity
    3. Using the cache from the base stage, builds and tests the following
       special configurations:
       * Address sanitizer (asan)
       * Undefined behavior sanitizer (ubsan)
       * A thread sanitizer (tsan) job is defined, but disabled, because it
         currently fails to run.
    4. There is no stage 4 in this workflow.
4. `linux-gcc8-debug.yml` stages:
    1. There is no stage 1 in this workflow, but if there is an appropriate
       NIH cache available from `linux-general.yml`, it will be used.
    2. Builds and tests a base `rippled` using
       * gcc-8
       * Debug
       * unity
    3. Using the cache from the base stage, builds and tests the following
       special configurations:
       * Coverage
       * Non-static
       * Non-static with shared libraries
       * Makefile (instead of Ninja)
       * Minimum cmake version
       * The separate
         [`validator-keys`](https://github.com/ripple/validator-keys-tool)
         application
    4. Uses the `rippled` artifact from the base job to run all of the manual
       tests defined in `ripple` except a few that use too much memory to run
       on Github's hosts.
5. `linux-gcc8-release.yml` stages:
    1. There is no stage 1 in this workflow, but if there is an appropriate
       NIH cache available from `linux-general.yml`, it will be used.
    2. Builds and tests a base `rippled` using
       * gcc-8
       * Release
       * unity
    3. There is no stage 3 in this workflow.
    4. Uses the `rippled` artifact from the base job to run all of the manual
       tests defined in `ripple` except a few that use too much memory to run
       on Github's hosts.

#### MacOS

6. `macos.yml` stages:
    1. Pre-downloads dependencies into an "NIH" ("Not Invented Here") cache,
       **and** builds Boost into a separate cache if not already cached. Two
       caches are used because Boost changes very rarely, and is not modified
       by the `rippled` build process, so it can be used across many
       different jobs without conflict, and saving space.
    2. Builds and tests `rippled` using:
       * Debug and Release
    3. There is no stage 3 in this workflow.
    4. Uses the Debug `rippled` artifact from the base job to run the IPv6
       tests using the `--unittest-ipv6` command line parameter.

#### Windows

7. `windows.yml` stages:
    1. Pre-downloads dependencies into an "NIH" ("Not Invented Here") cache,
       **and** builds vcpkg library dependencies and boost into separate
       caches if not already cached. Three caches are used because the
       vcpkg and boost libraries change very rarely, and are not modified
       by the `rippled` build process, so they can be used across many
       different jobs without conflict, and avoid duplication across jobs.
    2. Builds and tests `rippled` using the following configurations.
       (Note that MSVC 2019 Debug builds do not run tests by
       default due to unresolved issues with how it handles `static
      constexpr char*` variables. The tests can be forced to run by
       including "ci_run_win" in the git commit message.)
       * Ninja generator, MSVC 2017, Debug, unity
       * Ninja generator, MSVC 2017, Release, unity
       * Ninja generator, MSVC 2019, Debug, unity
       * Ninja generator, MSVC 2019, Release, unity
       * Visual Studio 2019 generator, MSVC 2017, Debug, non-unity
       * Visual Studio 2019 generator, MSVC 2019, Release, non-unity
    3. There is no stage 3 in this workflow.
    4. There is no stage 4 in this workflow.

##### Cacheing

###### Strategy

[Github Actions caches](https://docs.github.com/en/actions/guides/caching-dependencies-to-speed-up-workflows#usage-limits-and-eviction-policy)
are immutable once written, and use explicit key names for access. Caches can
be shared across workflows, and can match partial names by prefix. They can
also access caches created by base and default branches, but not across forks
and distinct branches. Finally, they have a relatively short expiration time
(7 days), and a relatively small size limit (5Gb).

The caching policies used by these workflows attempt to take advantage of
these properties to save as much time as possible when building, while
minimizing space when feasible. There is almost certainly room for
improvement.

Thus, for example, the `linux-general.yml` workflow downloads the "NIH"
dependencies into a single cache (per docker image). All of the source files
for these dependencies are stored in a single folder, which saves space in
the shared cache. Unfortunately, `cmake` is not always smart enough to know
that the source has already been downloaded, so there is some wasted effort
there, but because the same paths are used for unity and non-unity builds, it
still saves about half of that time.

Each "base" job then uses that cache to initialize its specific cache. During
the build, any additional dependencies are stored in the cache, along with
`ccache` output. This significantly speeds up subsequent builds of the same
base job.

Once the base job is done, any "special case" jobs (e.g. Coverage, address
sanitizer) use their base job's cache to initialize their specific caches.
This further helps reduce duplicated effort, which helps speed up those jobs.

Finally, most caches build their cache key using values from the job
configuration and some components based on the hashes of the `cmake`
configuration, the rippled source code, and the workflow config itself.

To pull it all together with an example, the base job in
`linux-clang8-debug.yml` might have a cache key that looks like (hashes
abbreviated for readability and simplicity):
* `Linux-ecbd-clang-8-Release-ON-base-40b1-5fec-a88b`
Once that job finishes, the "asan" job's cache key would look like:
* `Linux-ecbd-clang-8-Release-ON-asan-40b1-5fec-a88b`
    * It would be initialized using `Linux-ecbd-clang-8-Release-ON-base-40b1-5fec-a88b`.

Once the whole workflow finishes, the developer makes a change to source and
pushes a new commit. The new cache key might look like.
* `Linux-ecbd-clang-8-Release-ON-base-abcd-1234-a88b`
No cache with that key is found, so it looks for
* `Linux-ecbd-clang-8-Release-ON-base-abcd-1234`
* `Linux-ecbd-clang-8-Release-ON-base-abcd`
* `Linux-ecbd-clang-8-Release-ON-base`
That last prefix matches the cache from the previous run
(`Linux-ecbd-clang-8-Release-ON-base-40b1-5fec-a88b`), and initializes
with that. Chances are that most of that cache will be useful to that new
build, and will cut the build time significantly.

Once the base job finishes, the "asan" job's cache key would be:
* `Linux-ecbd-clang-8-Release-ON-asan-abcd-1234-a88b`
And would initialize from the just-finished
`Linux-ecbd-clang-8-Release-ON-base-abcd-1234-a88b`

The components are organized in the following order
* Operating system: Caches aren't useful to be shared across OSes
* Hashes of the `cmake` config: Any changes to the `cmake` config can have
  significant changes on the way that code is organized, dependencies are
  organized, dependency folders are organized, etc., which would render the
  caches incompatible. So to be safe, caches with different `cmake` configs
  can never be reused.
    * Additionally, this hash includes the file
      `.github/workflows/cache-invalidate.txt`. This file can be manually
      changed to force new builds to start with fresh caches in case some
      unforseen change causes the build to fail with a reused cache.
* Compiler
* Build type (Debug/Release)
* Unity flag
* Job name
* Hash of all the header files under `src`: Because changing one header file
  is more likely to affect a bunch of different object files, when this
  changes, it invalidates more.
* Hash of all the source files under `src`, including headers: Changing any
  source file is going to generate a new build. But with the same header
  hash, a build cache that is likely to be very similar can be reused.
* Hash of the workflow instructions (the yml file, and the
  `build-action/action.yml` if appropriate). If the workflow is changed
  without changing any of the source, a new cache may be needed, but it can
  be seeded with a previous build of the same source code.


## Action

The `build-action` folder holds an `action.yml` that is used by all of the
Linux workflows to do the actual build without tons of duplication.
Unfortunately, not all types of commands can by used in an action, so there
is still some boilerplate required in each job: checkout, cache, artifacts,
plus any steps that get displayed in a separate section in the Github UI.