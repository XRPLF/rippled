# `maybe_const.h` — Conditional Const Type Trait

## Role and Purpose

`maybe_const.h` is a small but purposeful metaprogramming utility in the `beast` namespace. It solves a recurring C++ template design problem: when a single template class needs to expose both mutable and immutable views of internal data, controlled by a boolean template parameter, selecting `T` vs. `const T` inline with `std::conditional` becomes verbose and error-prone. `maybe_const` encapsulates that selection cleanly.

## Design

The template struct `maybe_const<bool IsConst, class T>` wraps a single `std::conditional` to produce either `const T` or `T`:

```cpp
template <bool IsConst, class T>
struct maybe_const {
    using type = typename std::conditional<
        IsConst,
        typename std::remove_const<T>::type const,
        typename std::remove_const<T>::type>::type;
};
```

A deliberate detail here is the `std::remove_const` applied to `T` before re-adding `const`. This normalises the input: if a caller accidentally passes `const U` as `T` with `IsConst == false`, the result is still plain `U` rather than `const U`. Symmetrically, when `IsConst == true`, `remove_const` strips any existing qualifier before the canonical `const` is applied, preventing accidental `const const T` constructions that some compilers warn about or that complicate downstream deduction. This defensive normalisation is the non-obvious part of the design.

The companion alias `maybe_const_t<IsConst, T>` eliminates the `typename …::type` boilerplate at use sites, which is particularly valuable inside nested template contexts.

## How It Is Used in the Codebase

The only consumer found in the repository is `src/xrpld/peerfinder/detail/Livecache.h`, and it illustrates the pattern perfectly. `Livecache` maintains a fixed-size array of `list_type` buckets, one per hop count. It exposes iteration over those buckets through a `hops_t` helper that offers both mutable `iterator` and immutable `const_iterator` types via `boost::transform_iterator`.

The transform functor is itself templatised on `bool IsConst`:

```cpp
template <bool IsConst>
struct Transform {
    Hop<IsConst>
    operator()(
        typename beast::maybe_const<IsConst, typename lists_type::value_type>::type& list) const;
};
```

Without `maybe_const`, this functor would need two separate specialisations — one taking `list_type&` and one taking `const list_type&` — doubling the boilerplate. With `maybe_const`, a single template covers both cases: `Transform<false>` receives a mutable reference, `Transform<true>` receives a const reference, and the compiler resolves the difference entirely through the boolean parameter. The same pattern repeats in `make_hop` and in `Hop`'s stored `std::reference_wrapper`.

## Why This Pattern Over the Obvious Alternative

The straightforward alternative — two separate iterator/functor types — works but violates DRY. Any logic change to the hop-iteration functor would need to be duplicated, and the mutable/immutable symmetry would need to be maintained manually. The `IsConst` boolean parameter approach keeps the invariant that const and non-const traversal are structurally identical, enforced at compile time by sharing a single template body.

This is a well-known C++ idiom sometimes called the "const propagation template parameter" pattern. `maybe_const` gives it a named, reusable home rather than scattering `std::conditional<IsConst, const T, T>` spellings across the codebase.