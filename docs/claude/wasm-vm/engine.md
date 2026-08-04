[← Rust WASM VM docs](index.md)

# How the engine works

Contracts worth knowing before changing anything, and the reasons that are not visible in
the code.

**Two channels for a result.** A value or a guest-actionable error is the `i32` the wasm
function returns (`>= 0` value, `< 0` a `HostError` code). A **host-fatal** error —
`OutOfGas`, `Internal`, `NoMemExported` — traps instead, carrying `FatalHostError(HostError)`
as the payload so `run` can name the condition with `downcast_ref` rather than
string-comparing a message. XLS-0102 requires immediate halting on gas exhaustion, and a
guest handed `OutOfGas` as a code would run to the end of its current basic block — a
stopping point wasmi's `ConsumeFuel` placement decides rather than the protocol.
`is_fatal` spells the set variant by variant so a new `HostError`'s channel is chosen, not
inherited from its number. **`OutOfTransferLimit` is soft**: the one budget a contract can
be expected to handle.

`is_fatal` and `vm::host_fatal` are two lists that must agree. One direction is
compiler-enforced (`host_fatal` is exhaustive, so a new variant fails to build);
`every_fatal_error_has_an_outcome_of_its_own` covers the other. `HostError::ALL` and
`HostFunctionSpec::ALL` exist because an exhaustive `match` forces you to *write an arm* but
cannot *enumerate* variants, and every const-assertion scheme over `ALL` is beaten by "add
the variant, give its arm a value, leave `ALL` alone". The airtight mechanism is a single
declaration site: a `host_errors!` macro emits the enum, `ALL` and `from_code` from one list.

**Every failure carries its cost.** `run` returns `Result<RunOutcome, RunFailure>` where
`RunFailure` is `{ error: RunError, fuel_used }`, so gas is on both paths by construction. A
cost that cannot be read becomes `RunError::Internal` rather than a number — `0` would
forgive a run its whole cost and `gas` would charge an untouched one for everything.
`guest_halted` asks "did the guest halt?" at *every* stage from instantiation on, so a start
section that burns the limit is `OutOfGas`, not `Instantiate`: the stage a run stopped at is
not what the caller maps.

**Two ways a byte answer reaches the guest**, both taking a `Region`:

- `write_into` — the host writes straight into the guest's output region. Used by calls with
  no byte input. Zero copy.
- `write_buffered` — the host fills `VmState::out_buffer` and the engine copies it to the
  guest once every rule has passed. Used by calls that also *read* guest memory, because a
  `&mut` view of that memory admits no simultaneous `&` view. `Memory::data_and_store_mut`
  (`memory/mod.rs:165`) returns `(&mut [u8], &mut T)` — guest bytes and store data in one
  split borrow — which is what lets any number of inputs stay borrowed while the answer is
  written. No copying inputs out, no `unsafe`.

`write_buffered` never tells the host the guest's capacity: it offers the whole buffer and
takes the value's true length, so nothing reaches guest memory until the length, bounds, fit
and budget have all passed — **a refused value reaches it in no part**, which `write_into`
can only bound rather than prevent. The output is judged *after* the inputs, so a call with
both malformed reports the input's verdict; `NoMemExported` precedes both, because there is
no memory to validate a region against. `MAX_FIELD_BYTES` is checked beside the clamp on
purpose: the clamp bounds the **bytes**, the check bounds the **status**, since a host
reports a true length that can exceed the region it was offered.

Why this shape: of the ~65 ABI entries, **38 have a byte input *and* a byte output**, 9 are
output-only, 18 are input-only or scalar. Of the 38, **22 have more than one region** — every
two-argument keylet, `nft_uri`, all four float arithmetic ops — which a one-input helper
cannot express at all. The 9 output-only ones are exactly the row a buffer makes worse, and
they keep `write_into`.

**`Region`** (`region.rs`) is the wire's `(ptr, len)` as one type. It cannot catch a swapped
pair — `Region::new(len, ptr)` compiles, and no type can do better where the values arrive
as indistinguishable `i32`s in positional order; that is a job for a reader or for the shim
generator. What it enforces is that the pair cannot be *used* unchecked: `range()` is the
only way to indices, and it is where `InvalidParams` (the conversion to `usize` is the
negativity check) and the end-overflow guard live. It sits in its own module because Rust
privacy is module-level — inside `abi.rs` the helpers could still read `.ptr` and skip the
check. Verified: an attempted bypass is `error[E0616]: field ptr of struct Region is
private`. Construction is **infallible on purpose**; validating in `new` would hoist the
output region's verdict above the host call and break the input-first order that
`a_read_write_checks_its_input_before_its_output` pins.

`Region::read` is then ordinary safe slicing, because a guest pointer is an *index*: wasm
linear memory is a byte array in the store, `mem.data(caller)` is a `&[u8]` over it, and
`data.get(start..end)` does the bounds check and returns a slice **aliasing** guest memory.
`get` rather than `[..]` because indexing panics, and a panic on a consensus path is a node
crash. Elision ties the returned slice's lifetime to `data`, so a host cannot stash an input
past the call.

**Two budgets.** Gas is charged per host call from the spec table, before the body runs
(`charged` is the one path, so it cannot be forgotten); exhaustion spends what is left. The
transfer budget counts only bytes *copied* across — `charge_transfer` has one call site per
write path. A borrowed read copies nothing and is not charged; what bounds how many reads a
run makes is gas. Typed reads that materialise a host object will charge; this ABI has none
yet, and the alignment-copy charge for unaligned field reads has nothing to attach to until a
`FieldLocator` function exists.

**Guest memory is resolved once per run, by kind.** `run` takes
`instance.exports(&store).find_map(Export::into_memory)` after `instantiate_and_start` and
keeps the handle in `VmState::memory`, so no call pays for an export lookup. By *kind*, never
by name: nothing in the wasm spec attaches meaning to `"memory"`. Caching is sound because a
`Memory` is an arena index, not a pointer — it survives `memory.grow`. Two consequences:
the field assumes **one module, one instance, one store per `run`** (module linking would
have to resolve per instance, or serve a call against the wrong memory), and **a start
section cannot make a host call needing memory** — `Module::instantiate` is `pub(crate)`, so
instantiation cannot be split from the start section. `a_start_section_cannot_make_a_host_call`
pins it.

**Engine config is consensus-fixed**: fuel on, floats off, every post-MVP proposal off, one
process-wide `Engine` behind a `LazyLock` (an `Engine` is internally `Arc`ed and `Send +
Sync`). Notably `wasmi = { default-features = false, features = ["std"] }` — wasmi's `wat`
feature is **on by default** and makes `Module::new` accept text as readily as binary, which
would put a text assembler in the consensus path and make a transaction's validity a build
flag. `the_vm_refuses_a_text_format_module` catches that coming back, and
`WasmVMTest.TextFormatModuleIsRejected` catches it from the guest's side.

**A start section cannot be rejected outright.** wasmi 1.1 exposes no
`InstancePre`/`ensure_no_start` and `ModuleHeader::start` is private, so only a byte-level
section scan would do it. It is metered and memory-capped regardless, since `run` installs
the fuel and the limiter before `instantiate_and_start`.

**A dead end, recorded so nobody retries it.** Host-function parameters cannot be newtypes.
`wasmi::WasmTy` looks implementable — public, no sealing supertrait — but its bound names
`UntypedVal`, which wasmi re-exports only through a **private** `mod core`
(`wasmi-1.1.0/src/lib.rs:109-137`). Probed: `error[E0603]: module core is private`. The
escape hatch is a direct `wasmi_core` dependency pinned in lockstep with wasmi's own, plus a
`#[doc(hidden)]` method — not worth it on a consensus path. So the wire stays `i32` and pairs
are formed on the first line of each arm.

## Known gap: `OutOfGas` does not always report the whole limit

A contract that loops until the meter empties reports the full budget, but a budget too small
to reach the first charge reports `0`: wasmi leaves the remaining fuel in place on its own
`OutOfFuel` trap, and only `abi::charge` forces it to zero. The deleted C++ path did this
deliberately (`iw.setGas(0)` on out-of-gas, so the cost was always the full limit). Closing
the gap is a one-line change in `vm::run`, but it is consensus-visible metadata, so it is
called out rather than slipped in.
