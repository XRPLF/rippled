[← Rust WASM VM docs](index.md)

# Testing: the loop, and how the suites are built

## The build / test loop

- Fast: `cd crates && cargo check --workspace --all-targets`, `cargo test --workspace`,
  `cargo clippy --workspace --all-targets`.
- **`cargo doc -p xrpl-wasm-vm --no-deps` is part of the loop, not a nicety.** `lib.rs`
  carries `deny(rustdoc::broken_intra_doc_links)`, and neither `cargo test` nor `clippy`
  checks doc links. **Caveat: it does not cover private modules**, which are not documented
  by default — a dead link inside `abi.rs` passes silently (this is how a `VmState::scratch`
  link survived the `out_buffer` rename). Add `--document-private-items` to check those, and
  grep after renaming a field. `lib.rs` also carries `forbid(unsafe_code)`,
  `deny(unreachable_pub)` and `deny` on four clippy cast lints, so an unargued cast fails the
  build rather than warning.
- Full C++↔Rust: normal CMake build, then
  `./xrpl_tests --gtest_filter='WasmVMTest.*:*Call.*'`.
- Guest-linkability (needs `rustup target add wasm32-unknown-unknown`):
  `cargo check -p xrpl-host-functions --target wasm32-unknown-unknown`. Only the ABI crate —
  `xrpl-wasm-vm` is host-side and pulls in wasmi, and `crates/hello_world` cannot be checked
  for that target at all because it depends on `cxx` → `link-cplusplus`, which wants a C++
  toolchain for the target.
- VCS is **jj** (`jj st`, `jj log`), not raw git, for local work.

### Two build gotchas that cost an afternoon each

- **A stale build directory fails to link with `duplicate symbol '_rust_eh_personality'`.**
  The conan `wasmi` package ships a Rust `std`, and so does our staticlib. `b7059deb9f`
  dropped the conan requirement but left `find_package(wasmi REQUIRED)` and `wasmi::wasmi` in
  the CMake, both now removed; a build folder generated before that still has
  `build/generators/wasmi-*.cmake`, so re-run `conan install .. --output-folder . --build
  missing --settings build_type=Debug` and delete them. Note this was *two differently
  compiled* `std`s — two staticlibs from this workspace are fine, and `xrpld` already links
  `rs_hello_world` alongside `xrpl_wasm_vm_ffi`.
- **`cargo test` on the bridge crate links only because nothing in the tests reaches a C++
  shim.** The `extern "C++"` symbols exist only in the CMake build, and the test binary links
  because `-dead_strip` drops what no test path reaches. Verified: forcing a reference
  (`let f: fn(&ffi::HostContext) -> _ = ...`) fails with `Undefined symbols:
  _rs$wasm_vm$cxxbridge1$…`. So keep those tests on pure logic — the status map, the panic
  guard, the wire conversions — and put anything that needs a host in the gtest.

## The Rust tests

- **They come in two kinds, and the split is forced.** A wasmi `Caller` exists only during a
  host call, so everything in `abi.rs` that takes one cannot be reached from a unit test.
  Unit tests in `src/` cover what needs no live instance (wire conversions, budget
  arithmetic, the limits); guest-memory policy lives in `tests/`, running real modules
  against a configurable fake host.
- Those integration tests write modules as **WAT text** and assemble it themselves — `wat` is
  a `[dev-dependencies]` entry and `support::assemble` its only caller, so the assembler
  never enters the library. `run` takes binaries; there is no `run_wat`.
- `tests/support/mod.rs` holds the fake host and the import declarations. `Answer` separates
  *what the host writes* from *what length it reports*, which is what makes the over-cap and
  buffer-fit rules testable without values that large existing.

## How the C++ tests are built

`src/tests/libxrpl/tx/wasm/`, in the `xrpl_tests` gtest binary. Four decisions, each of which
had an obvious cheaper alternative that was worse.

**Modules are WebAssembly text, assembled at run time.** Checked-in hex blobs do not scale
past one module — every host function needs its own, with its own import signature — and they
are unreviewable. So `compile_wat` comes over cxx from **`crates/xrpl-wasm-testkit`**, a crate
of its own that nothing in `libxrpl` or `xrpld` links.

That separation is the whole point and is worth not undoing. The engine pins
`wasmi = { default-features = false }` because wasmi's `wat` feature makes `Module::new`
accept text as readily as binary, which would make a transaction's validity a build flag
(finding A5 in [history.md](history.md)). Putting `compile_wat` on the production bridge would
link an assembler into the shipped node even though nothing called it; a cargo feature would
make the test and production binaries differ. A separate crate makes "no assembler in the
node" a property of the link graph. `WasmVMTest.TextFormatModuleIsRejected` then feeds the
engine the very text the rest of the suite assembles, so the guest-side half of A5 is pinned
too.

**The host is a `StrictMock`.** `MockHostFunctions` mocks only the methods the ABI declares;
the ~60 others keep `HostFunctions`' `Unimplemented` default, so a contract reaching past the
ABI fails the way production would. What this buys over a hand-written fake is assertions on
*what the host was asked* — that a guest `i32` became the right `SField`, that two borrowed
regions and a flag all arrived, that an `i64` survived as `INT64_MIN`.

Strict rather than nice, because these modules import exactly what they mean to exercise: a
host call no test asked for means the engine reached for something on its own, which is worth
a failure rather than a warning. The cost is one line in the fixture —
`EXPECT_CALL(host_, checkSelf()).WillRepeatedly(Return(true))`, since `runEscrowWasm` asks
every run whether the host is clean. Verified by mutation: giving `escrow_finish` an
unstubbed host call fails the test under Strict and passes silently under `NiceMock`.

*One trap worth knowing even so*: gmock's default action for `std::expected<T, E>` is a
**successful** `T{}`, so a method with an `EXPECT_CALL` but no action would answer `0` and a
test could pass on an answer nobody chose. The mock's constructor therefore `ON_CALL`s every
method to the base class's `std::unexpected(Unimplemented)`.

**Two levels of fixture.** `WasmTest` holds the mock, a capturing journal sink and `run(wat,
gas, entryPoint)`. `HostCallTest` adds a `wat()` the derived fixture supplies and
`hostAnswer()`, so a per-function test says only what the host was asked and what came back.
Then one fixture per host function — `LedgerSqnCall`, `CurrentLedgerObjFieldCall`,
`Sha512HalfCall`, `TraceCall`, `TraceNumCall` — because the module *is* that function's shared
setup. `WasmVMTest` keeps what belongs to the engine rather than to any function.

**The journal is captured, not sent to a null sink.**
`WasmVMTest.ThrowingHostFunctionBecomesInternal` asserts the exception text *and* that the log
names `getLedgerSqn`; without that, an exception silently swallowed with no log would pass, and
`HostContext::guarded`'s `source_location` would be untested.

Two properties are pinned from the guest's side rather than asserted about internals:
`LedgerSqnCall.BufferTooSmallIsRefusedWholeNotTruncated` has the contract report whether
*anything* reached its memory, which is "a refused value reaches it in no part" as a contract
can observe it; and `WasmVMTest.SoftHostErrorCodesCrossUnchanged` walks all 18 soft
`HostFunctionError` codes, because the C++ and Rust error enums are two hand-maintained lists
of the same numbers that **have already drifted once** — -11 is `OutOfTransferLimit` in C++ and
`Decoding` in the Rust ABI; see "the two error enums have already drifted" in
[open-questions.md](open-questions.md).

*Mutation-checked*: making a too-large value write a truncated prefix, and pointing the
sha512 input matcher at bytes the guest does not send, each fail exactly one test and nothing
else.

**Naming.** Subject-first, no leading article — `ContractReturnValueReachesCaller`, not
`AContractsReturnValueReachesTheCaller`. That is the house style in `src/tests/libxrpl`
(`BuilderThrowsOnWrongEntryType`, `OptionalFieldsReturnNullopt`).

## The old C++ suites, and why they are not the parity oracle yet

`src/test/app/Wasm_test.cpp` and `HostFuncImpl_test.cpp` are **entirely inside `/* */`** and
compile to nothing, as is `src/libxrpl/tx/wasm/WasmiVM.cpp`.

`Wasm_test.cpp` asserts exact gas numbers (e.g. 29'502), which makes it the best gas-parity
oracle available — but **its fixtures cannot run on this engine.** They import from module
`env`, not `host_lib` (`kLedgerSqnWasmHex` decodes to
`... 03 656e76 0a 6c6467725f696e646578 ...`), and their `target_features` include `sign-ext`,
`multivalue` and `reference-types`, which this engine disables. The deleted C++ engine ignored
the import module name entirely — `wasm_importtype_module` is commented out at its
`WasmiVM.cpp:428`. Reviving it as an oracle means recompiling those fixtures with
`-Wl,--import-module=host_lib` and the engine's feature set. The gtest carries its own
WAT-derived modules for that reason.
