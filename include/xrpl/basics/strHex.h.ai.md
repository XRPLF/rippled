# `strHex.h` — Binary-to-Hex String Conversion

This header lives in `xrpl/basics` and provides the single utility `strHex`, a thin but carefully constructed wrapper around `boost::algorithm::hex`. Its role is to produce uppercase hexadecimal `std::string` representations of arbitrary binary data, and it is used pervasively across the XRPL codebase wherever raw bytes must be serialized for logging, JSON output, or human-readable display.

## The Two Overloads

The iterator-range form is the core implementation:

```cpp
template <class FwdIt>
std::string strHex(FwdIt begin, FwdIt end)
```

It enforces at compile time — via `static_assert` — that `FwdIt` satisfies at least the `std::forward_iterator_tag` requirement. This is not a bureaucratic constraint: the implementation calls `std::distance(begin, end)` to pre-reserve the output string before delegating to `boost::algorithm::hex`. Forward iterators are guaranteed to be multi-pass, so the range can be measured and then iterated again. An input iterator is single-pass and would silently produce wrong results or undefined behavior if `distance` consumed it. The assertion catches this class of misuse at compile time rather than at runtime.

The pre-reservation (`result.reserve(2 * std::distance(begin, end))`) is a deliberate performance optimization. Each input byte encodes to exactly two hex characters, so the output capacity is always known up front. Without it, `std::back_inserter` would trigger repeated reallocations as `boost::algorithm::hex` appends characters — potentially O(n log n) copies instead of O(n).

The container overload is a convenience SFINAE shim:

```cpp
template <class T, class = decltype(std::declval<T>().begin())>
std::string strHex(T const& from)
```

The second template parameter `class = decltype(std::declval<T>().begin())` is a lightweight concept check that participates in overload resolution only for types that have a `begin()` member. This makes `strHex` uniformly callable on `std::string`, `std::vector<uint8_t>`, `Blob`, `Slice`, and `base_uint` without requiring explicit specializations. The check is intentionally loose — it does not verify that `end()` exists or that the iterator satisfies the forward requirement — relying instead on the inner overload's `static_assert` to catch any mismatch at instantiation time.

## Integration With the Basics Module

`Slice.h` includes `strHex.h` and uses it directly in its stream insertion operator:

```cpp
template <class Stream>
Stream& operator<<(Stream& s, Slice const& v) {
    s << strHex(v);
    return s;
}
```

This makes any `Slice` — the ledger's canonical immutable byte-range view — directly loggable and JSON-serializable as hex. `base_uint.h` similarly pulls in `strHex.h` and uses both overloads: `to_string(base_uint)` calls `strHex(a.cbegin(), a.cend())` to render full 256-bit or 160-bit hashes, while a truncation helper uses `strHex(a.cbegin(), a.cbegin() + 4) + "..."` for compact diagnostic output.

RPC handlers use `strHex` directly when building JSON responses — for example, `LedgerHeader.cpp` serializes raw ledger header bytes into the `ledger_data` JSON field via `strHex(s.peekData())`, where `peekData()` returns a `std::vector<uint8_t>`.

## Design Notes

The header has no corresponding `.cpp` file — everything is header-only template code. There is no integer overload (e.g. for `uint32_t` or `uint64_t`), which is a deliberate omission: integer-to-hex formatting carries questions about byte order and padding width that are better handled explicitly at call sites. `boost::endian` is included but not directly used within this header; it appears to be an indirect dependency carried by callers that deal with endian-correct serialization before passing bytes to `strHex`.

The result is always uppercase hex (Boost.Algorithm.Hex produces uppercase by default), which aligns with the XRPL convention of presenting all hashes and raw binary in uppercase hexadecimal throughout its JSON API and log output.