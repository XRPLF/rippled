[← Rust WASM VM docs](index.md)

# Conventions

**Comments.** Terse. A comment should say something the compiler cannot check and the code
cannot show; everything else is a candidate for deletion. Keep: why an apparent redundancy is
not one (the `MAX_FIELD_BYTES` check beside the clamp; `is_fatal`/`host_fatal` as two lists;
`MUST_TRAP` not deriving from `is_fatal` — each of these has been "simplified" wrongly in a
mutation test at least once); load-bearing invariants; hidden contracts a signature cannot
state; wasmi facts that decide a design. Cut: prose restating the next line; the same
rationale on a field and on its reader; retellings of these docs.

**No references to C++ that will not survive the merge.** They read as evidence but point at
deleted files. The crate has none, in `src/` or `tests/`. Two live exceptions stand:
`Protocol.h`'s `kMaxWasmDataLength` and `kWasmTransferLimit`, which are where those numbers
are defined for the rest of the system. The parity evidence itself lives in
[history.md](history.md) instead, which is commit-pinned and therefore stays resolvable.

**No historical comments** in code — describe the present, not how it differs from a previous
state.

**C++ naming.** rippled's camelBack for methods, `k`-prefixed CamelCase for constants; the
bridge keeps ABI names on the Rust side and camelBack on the C++ side via `cxx_name`. Test
names are subject-first with no leading article ([testing.md](testing.md)).
