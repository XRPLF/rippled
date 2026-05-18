# `Main.cpp` — Server Entry Point and CLI Orchestration

`Main.cpp` is the outermost shell of the `xrpld` daemon. It owns the binary's `main()` function, translates raw command-line arguments into typed configuration, and dispatches to exactly one of three mutually exclusive execution paths: the full server, an RPC client call to a running server, or the unit test suite. Every other component in `app/main/` — `ApplicationImp`, `BasicApp`, `LoadManager`, `NodeIdentity`, and friends — exists downstream of the decisions made here.

## The `main()` Function and Platform Setup

`main()` is deliberately thin. On Windows it primes timezone initialization via an `_ftime` call before spawning any coroutines, working around a Boost bug (ticket #10657) where `GetTimeZoneInformation` misbehaves when first called from a coroutine context. It registers `google::protobuf::ShutdownProtobufLibrary` via `atexit` to ensure clean protobuf teardown on exit, then immediately delegates to `xrpl::run()`. This separation quarantines platform-specific setup and makes the real logic independently callable.

Before the first line of runtime code, compile-time `#error` directives enforce that exactly one of Linux, Windows, or macOS is detected by the Boost.OS macros. A second check ensures no two platform macros are simultaneously active. These are zero-cost build-time assertions that catch misconfigured toolchains or future platform additions that haven't been audited for correctness.

## The `run()` Function: Decision Tree and Configuration Assembly

`run()` starts by naming its thread `"xrpld-main"` via `beast::setCurrentThreadName`, which makes the thread identifiable in debuggers and log output. It then builds a `boost::program_options` descriptor in three visible groups (`gen`, `data`, `rpc`) plus a hidden group. The hidden group contains `--unittest-child` and the deprecated `--fg` flag — options that must be accepted without producing parse errors but should never appear in help text. Positional arguments are mapped to `--parameters`, which activates the RPC client path.

After parsing, `run()` follows a strict linear decision tree:

1. **`--help` / `--version`** — Print to stdout/stderr and exit before touching the filesystem or constructing any objects.
2. **`--unittest`** (when `ENABLE_TESTS` is defined) — Delegate to `runUnitTests()` and exit with its result code.
3. **Full `Config` construction** — `config->setup()` reads the config file, resolves paths, and applies `--quiet`, `--silent`, and `--standalone` flags.
4. **`--vacuum`** — Opens the transaction database and runs `doVacuumDB`, then exits.
5. **Startup mode resolution** — Sets `config->START_UP` to the appropriate `StartUpType` enum value based on combinations of `--ledger`, `--replay`, `--load`, `--net`, `--start`, and `--ledgerfile`.
6. **Positional parameters present** — Acts as an RPC client via `RPCCall::fromCommandLine`.
7. **No parameters** — Constructs and runs the full server.

The ordering is load-bearing: the unit test path and RPC paths both exit before the two-phase file descriptor adjustment and full `Application` construction, keeping those lightweight paths lean.

Rather than plumbing individual flags into `ApplicationImp` as separate arguments, `run()` builds a `Config` object and decorates it with all CLI overrides. Mutually exclusive combinations are caught inline: `--net` with `--load` or `--replay` is rejected with a human-readable message before anything expensive starts. The `--trap_tx_hash` / `--replay` dependency is similarly enforced — trap logic is meaningless outside of ledger replay, so using `--trap_tx_hash` alone is an immediate error. The trap hash itself is validated with `uint256::parseHex` before being stored, catching malformed hex strings at the earliest possible point.

The `--force_ledger_present_range` option is a testing escape hatch that overrides the node's advertised present-ledger range. It parses a `"min,max"` string, validates `min <= max`, and stores it as `config->FORCED_LEDGER_RANGE_PRESENT`. Keeping this validation inline rather than inside `Config::setup()` intentionally separates the testing concern from the production configuration path.

## File Descriptor Management: `adjustDescriptorLimit()`

`adjustDescriptorLimit()` uses POSIX `getrlimit`/`setrlimit` to ensure the process holds at least as many open file descriptors as requested. It attempts to raise the soft limit if the current soft limit is below the requested count, intentionally ignoring `rlim_max` since processes can often be configured to exceed it. If the system still can't satisfy the request, the function logs a fatal message and returns `false`.

The function is called **twice** in the server path: once with a floor of 1024 before `Application` is constructed, and again after `app->setup()` returns `app->fdRequired()`. The two-phase approach solves a chicken-and-egg problem — you need *some* descriptors to open config files and database connections, but you don't know the true requirement until after setup has parsed peer counts and database configurations. If either call fails, `run()` returns `-1` immediately, because a descriptor-starved server will fail unpredictably later rather than cleanly.

## Unit Test Infrastructure

When built with `ENABLE_TESTS`, the file includes a local `multi_selector` class that wraps multiple `beast::unit_test::selector` instances, enabling comma-separated filter patterns like `--unittest "ripple.ledger,ripple.app"`. The OR semantics (any matching selector admits a suite) are intentional: AND semantics would make it impossible to run disjoint suites in one invocation.

`runUnitTests()` supports three execution modes controlled by the `child` flag and `num_jobs` count:

- **Single-job, non-child**: Creates `multi_runner_parent` and `multi_runner_child` in the same process. This is the normal developer workflow.
- **Multi-job parent**: Spawns `num_jobs` child processes via `boost::process::v1::child`, each receiving the original `argv` plus `--unittest-child`. The parent collects exit codes; signal-terminated children (caught by `catch(...)` around `c.wait()`) increment both the bad-exit and terminated-child counters, correctly propagating signal-based failures.
- **Child process**: Runs `multi_runner_child` directly, contributing results to the parent through the IPC mechanism in `multi_runner.h` (built on Boost.Interprocess shared memory and message queues).

The `anyMissing()` helper prevents a subtle false-success failure mode: if a filter pattern matches no test suites, the runner would report zero failures and exit with `EXIT_SUCCESS`, silently skipping tests the developer expected to run. `anyMissing` compares `runner.suites()` against `pred.size()` and treats each unmatched pattern as an explicit failure.

The `test::envUseIPv4` atomic (declared `extern` and defined in the test namespace) controls whether loopback addresses in tests resolve to IPv4 or IPv6, toggled via `--unittest-ipv6`. Declaring it `extern` here keeps test-infrastructure headers out of the main compilation unit.

## Ownership and Lifetime

`Main.cpp` is the only file in this directory that interacts directly with `boost::program_options`. All downstream components receive already-parsed data through the `Config` object or direct method arguments. The `Application` instance, the `Logs` object, and the `TimeKeeper` are all constructed here and transferred via `std::unique_ptr`, establishing the ownership and lifetime that `ApplicationImp` and the entire server stack depend on. Constructing `Logs` at the CLI-specified severity threshold before passing it in ensures that even earliest startup messages respect the user's verbosity preference rather than defaulting to a hard-coded level inside the application layer.