# Antithesis

We use [Antithesis](https://antithesis.com/) for continuous fuzzing, and keep
a copy of the [Antithesis C++ SDK](https://github.com/antithesishq/antithesis-sdk-cpp/)
in `external/antithesis-sdk`. One of the aims of fuzzing is to identify bugs
by finding external conditions that cause contract violations inside `xrpld`.
Contracts are expressed as `XRPL_ASSERT` or `UNREACHABLE` (defined in
`include/xrpl/beast/utility/instrumentation.h`), which outside of Antithesis
are effectively wrappers for `assert(...)` with an added name. The name gives
each contract a stable identity that does not rely on line numbers.

When `xrpld` is built with Antithesis instrumentation enabled (the `voidstar`
CMake option) and run on the Antithesis platform, the contracts become
[test properties](https://antithesis.com/docs/using_antithesis/properties.html);
otherwise they behave like a regular `assert`. To learn more about Antithesis,
see [How Antithesis Works](https://antithesis.com/docs/introduction/how_antithesis_works.html)
and the [C++ SDK](https://antithesis.com/docs/using_antithesis/sdk/cpp/overview.html).

We continue to use plain `assert` or `assert(false)` in certain locations where
reporting contract violations on the Antithesis platform is either not possible
or not useful.

For this reason:

- `assert` and `assert(false)` should continue to be used in:
  - `constexpr` functions;
  - unit tests, i.e. files under `src/test`;
  - unit-test-related modules (files under `beast/test` and `beast/unit_test`).
- Outside of these locations, do not use `assert`; use `XRPL_ASSERT` instead,
  giving it a unique name and a short description of the contract.
- Outside of these locations, do not use `assert(false)`; use `UNREACHABLE`
  instead, giving it a unique name and a description of the condition being
  violated.
- The contract name should start with the full name (including scope) of the
  function, optionally followed by a named lambda, then a colon `:` and a brief
  (typically at most five words) description. `UNREACHABLE` contracts can use
  slightly longer descriptions. If the function has multiple overloads, use
  common sense to balance brevity and unambiguity of the function name.
  NOTE: the name exists to identify every contract uniquely and stably, so
  avoid elements that are likely to change in obvious refactors or when the
  condition is strengthened.
- The description of a contract (except `UNREACHABLE`) should describe the
  _expected_ condition, as in "I assert that _expected_ is true".
- The description of an `UNREACHABLE` contract should describe the
  _unexpected_ situation that caused the line to be reached.
- Example of a good `UNREACHABLE` name:
  `"json::operator==(Value, Value) : invalid type"`; example of a good
  `XRPL_ASSERT` name: `"json::Value::asCString : valid type"`.
- Example of a **bad** name:
  `"RFC1751::insert(char* s, int x, int start, int length) : length is greater than or equal zero"`
  (missing namespace, unnecessary full function signature, description too
  verbose). Good name: `"xrpl::RFC1751::insert : minimum length"`.
- In a **few** well-justified cases a non-standard name can be used, in which
  case add a comment explaining the rationale (see
  `src/libxrpl/basics/contract.cpp` for an example).
- Do **not** rename a contract without a good reason (e.g. the name no longer
  reflects the location or the condition being checked).
- Do not use `std::unreachable`.
- Do not put contracts where they can be violated by an external condition
  (e.g. timing, or a data payload before mandatory validation), as this
  creates bogus bug reports (and crashes Debug builds).
