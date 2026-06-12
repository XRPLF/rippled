#pragma once

#include <lean/lean.h>

#include <concepts>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {
namespace test {
namespace lean4 {

// Owner of a lean_object*, and the base for every FFI wrapper. Refcounting lives here:
// subclasses go through the typed leanGet*/leanSet* helpers below and never touch it directly.
// Because Lean @[export] functions consume their object arguments, pass borrow() to a read and
// give() to a write; use raw() only to inspect a handle, never as an argument to a Lean call.
class LeanObjectFFI
{
    lean_object* o_ = nullptr;

protected:
    // fn(self) or fn(self, ownerCount).
    template <class R, class... A>
    R leanGet(R (*fn)(lean_object*, A...), A... args) const { return fn(borrow(), args...); }

    template <class W>
    typename W::CppType leanGetObj(lean_object* (*fn)(lean_object*)) const { return W(fn(borrow())).read(); }

    template <class W>
    W leanGetWrapped(lean_object* (*fn)(lean_object*)) const { return W(fn(borrow())); }

    template <class W>
    std::optional<W> leanOptWrapped(lean_object* opt) const
    {
        LeanObjectFFI hold(opt);
        return lean_obj_tag(hold.o_) == 1 ? std::optional<W>(W(retain_(lean_ctor_get(hold.o_, 0)))) : std::nullopt;
    }

    // Call fn(thisHandle, args...): receiver borrowed, args forwarded via leanPrep.
    template <class Fn, class... A>
    lean_object* leanCallSelf(Fn fn, A&&... args) const;  // leanPrep visible below

    // leanCallSelf whose Option result is wrapped as std::optional<W>.
    template <class W, class Fn, class... A>
    std::optional<W> leanOptCall(Fn fn, A&&... args) const
    { return leanOptWrapped<W>(leanCallSelf(fn, std::forward<A>(args)...)); }

    std::vector<uint8_t> leanGetBytes(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI b(fn(borrow())); uint8_t const* p = lean_sarray_cptr(b.o_); return std::vector<uint8_t>(p, p + lean_sarray_size(b.o_)); }

    template <class W>
    std::optional<typename W::CppType> leanGetOpt(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI o(fn(borrow())); return lean_obj_tag(o.o_) == 1 ? std::optional<typename W::CppType>(W(retain_(lean_ctor_get(o.o_, 0))).read()) : std::nullopt; }
    std::optional<uint32_t> leanGetOptU32(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI o(fn(borrow())); return lean_obj_tag(o.o_) == 1 ? std::optional<uint32_t>(lean_unbox_uint32(lean_ctor_get(o.o_, 0))) : std::nullopt; }
    std::optional<uint64_t> leanGetOptU64(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI o(fn(borrow())); return lean_obj_tag(o.o_) == 1 ? std::optional<uint64_t>(lean_unbox_uint64(lean_ctor_get(o.o_, 0))) : std::nullopt; }
    std::optional<uint8_t> leanGetOptU8(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI o(fn(borrow())); return lean_obj_tag(o.o_) == 1 ? std::optional<uint8_t>(static_cast<uint8_t>(lean_unbox(lean_ctor_get(o.o_, 0)))) : std::nullopt; }
    std::optional<std::vector<uint8_t>> leanGetOptBytes(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI o(fn(borrow())); if (lean_obj_tag(o.o_) != 1) return std::nullopt; lean_object* a = lean_ctor_get(o.o_, 0); uint8_t const* p = lean_sarray_cptr(a); return std::vector<uint8_t>(p, p + lean_sarray_size(a)); }

    template <class W>
    std::vector<typename W::CppType> leanGetList(lean_object* (*fn)(lean_object*)) const
    { LeanObjectFFI lst(fn(borrow())); std::vector<typename W::CppType> v;
      for (lean_object* c = lst.o_; lean_obj_tag(c) == 1; c = lean_ctor_get(c, 1)) v.push_back(W(retain_(lean_ctor_get(c, 0))).read());
      return v; }

    template <class V>
    void leanSet(lean_object* (*fn)(lean_object*, V), V v) { reset(fn(give(), v)); }
    template <class W, class C>
    void leanSetObj(lean_object* (*fn)(lean_object*, lean_object*), C const& v) { reset(fn(give(), W::build(v).give())); }
    template <class W, class C>
    void leanSetOptObj(lean_object* (*fn)(lean_object*, lean_object*), C const& v) { reset(fn(give(), leanSome(W::build(v)).give())); }
    template <class W, class C>
    void leanSetOptHandle(lean_object* (*fn)(lean_object*, lean_object*), std::optional<C> const& v);  // leanOptHandle visible below
    void leanSetOptU32(lean_object* (*fn)(lean_object*, lean_object*), uint32_t v);
    void leanSetOptU64(lean_object* (*fn)(lean_object*, lean_object*), uint64_t v);
    void leanSetOptU8(lean_object* (*fn)(lean_object*, lean_object*), uint8_t v);
    template <class Bytes>
    void leanSetBytes(lean_object* (*fn)(lean_object*, lean_object*), Bytes const& v);
    template <class Bytes>
    void leanSetOptBytes(lean_object* (*fn)(lean_object*, lean_object*), Bytes const& v);
    template <class W, class C>
    void leanSetList(lean_object* (*fn)(lean_object*, lean_object*), std::vector<C> const& xs)
    { lean_object* lst = lean_box(0);
      for (auto it = xs.rbegin(); it != xs.rend(); ++it) { lean_object* cell = lean_alloc_ctor(1, 2, 0); lean_ctor_set(cell, 0, W::build(*it).give()); lean_ctor_set(cell, 1, lst); lst = cell; }
      reset(fn(give(), lst)); }

    static lean_object* retain_(lean_object* o) { lean_inc(o); return o; }
    void reset(lean_object* o) noexcept { if (o_) lean_dec(o_); o_ = o; }
    template <class W> W leanBuildAs() { return W(give()); }
    static lean_object* leanEmptyOf(lean_object* (*fn)(lean_object*)) { return fn(lean_box(0)); }
    static lean_object* leanWrapCtor(uint8_t tag, lean_object* field0)
    { lean_object* e = lean_alloc_ctor(tag, 1, 0); lean_ctor_set(e, 0, field0); return e; }
    template <class W> W leanInnerAs() const { return W(retain_(lean_ctor_get(o_, 0))); }

public:
    LeanObjectFFI() = default;
    explicit LeanObjectFFI(lean_object* o) noexcept : o_(o) {}
    LeanObjectFFI(LeanObjectFFI const&) = delete;
    LeanObjectFFI& operator=(LeanObjectFFI const&) = delete;
    LeanObjectFFI(LeanObjectFFI&& other) noexcept : o_(other.o_) { other.o_ = nullptr; }
    LeanObjectFFI&
    operator=(LeanObjectFFI&& other) noexcept
    {
        if (this != &other) { reset(other.o_); other.o_ = nullptr; }
        return *this;
    }
    ~LeanObjectFFI() { reset(nullptr); }

    lean_object* raw() const noexcept { return o_; }  // borrow, NO inc (@& / inspect)
    lean_object* borrow() const noexcept { lean_inc(o_); return o_; }  // inc, for an owned callee
    lean_object* give() noexcept { auto t = o_; o_ = nullptr; return t; }  // transfer ownership out
};

// Any per-type wrapper around a Lean handle.
template <typename W>
concept LeanEntry = std::derived_from<W, LeanObjectFFI>;

// A fetch thunk: callable with no args, returns a raw (owned) Lean handle.
template <typename F>
concept LeanFetch =
    std::invocable<F> && std::same_as<std::invoke_result_t<F>, lean_object*>;

// A typed value wrapper: a LeanEntry that builds from / reads back its CppType.
template <typename W>
concept LeanWrapper = LeanEntry<W> &&
    requires(W const& w, typename W::CppType const& v) {
        { W::build(v) } -> std::same_as<W>;
        { w.read() } -> std::same_as<typename W::CppType>;
    };

// ByteArray (ids / Blob)
inline LeanObjectFFI
mkBytes(uint8_t const* p, size_t n)
{
    lean_object* a = lean_alloc_sarray(1, n, n);
    std::memcpy(lean_sarray_cptr(a), p, n);
    return LeanObjectFFI(a);
}

// Option: constructors + access
inline lean_object* leanNone() { return lean_box(0); }
inline LeanObjectFFI
leanSome(LeanObjectFFI v)
{
    lean_object* o = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(o, 0, v.give());
    return LeanObjectFFI(o);
}
inline LeanObjectFFI
leanSomeU32(uint32_t x)
{
    lean_object* o = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(o, 0, lean_box_uint32(x));
    return LeanObjectFFI(o);
}
inline LeanObjectFFI
leanSomeU64(uint64_t x)
{
    lean_object* o = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(o, 0, lean_box_uint64(x));
    return LeanObjectFFI(o);
}
inline LeanObjectFFI
leanSomeU8(uint8_t x)
{
    lean_object* o = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(o, 0, lean_box(x));
    return LeanObjectFFI(o);
}
inline bool isSome(lean_object* o) { return lean_obj_tag(o) == 1; }
inline lean_object* optionVal(lean_object* o) { return lean_ctor_get(o, 0); }

// Inc a borrowed handle and return it as an owned raw pointer (for building a wrapper).
inline lean_object* retain(lean_object* o) { lean_inc(o); return o; }

// Prepare a value to be passed as an owned arg to a Lean export
template <class W>
    requires std::derived_from<std::remove_cvref_t<W>, LeanObjectFFI>
inline lean_object* leanPrep(W const& w) { return w.borrow(); }
template <class T>
    requires(!std::derived_from<std::remove_cvref_t<T>, LeanObjectFFI>)
inline T leanPrep(T x) { return x; }

template <class Fn, class... A>
inline lean_object* LeanObjectFFI::leanCallSelf(Fn fn, A&&... args) const
{ return fn(borrow(), leanPrep(std::forward<A>(args))...); }

// Call a Lean export forwarding each arg through leanPrep()
template <class Fn, class... A>
inline auto leanCall(Fn fn, A&&... args) { return fn(leanPrep(std::forward<A>(args))...); }

inline lean_object*
leanOptU32(std::optional<uint32_t> const& o) { return o ? leanSomeU32(*o).give() : leanNone(); }
inline lean_object*
leanOptU64(std::optional<uint64_t> const& o) { return o ? leanSomeU64(*o).give() : leanNone(); }
inline std::optional<uint32_t>
readOptU32(lean_object* o) { return isSome(o) ? std::optional<uint32_t>(lean_unbox_uint32(optionVal(o))) : std::nullopt; }
inline std::optional<uint64_t>
readOptU64(lean_object* o) { return isSome(o) ? std::optional<uint64_t>(lean_unbox_uint64(optionVal(o))) : std::nullopt; }
inline lean_object*
leanOptU8(std::optional<uint8_t> const& o) { return o ? leanSomeU8(*o).give() : leanNone(); }
inline std::optional<uint8_t>
readOptU8(lean_object* o) { return isSome(o) ? std::optional<uint8_t>(static_cast<uint8_t>(lean_unbox(optionVal(o)))) : std::nullopt; }

template <class W>
inline lean_object*
leanOptHandle(std::optional<typename W::CppType> const& o) { return o ? leanSome(W::build(*o)).give() : leanNone(); }
template <class W>
inline std::optional<typename W::CppType>
readOptHandle(lean_object* o) {
    return isSome(o) ? std::optional<typename W::CppType>(W(retain(optionVal(o))).read()) : std::nullopt;
}

template <class Bytes>
inline lean_object*
leanOptBytes(std::optional<Bytes> const& o) { return o ? leanSome(mkBytes(o->data(), o->size())).give() : leanNone(); }
inline std::vector<uint8_t>
readBytes(lean_object* a) {
    uint8_t const* p = lean_sarray_cptr(a);
    return std::vector<uint8_t>(p, p + lean_sarray_size(a));
}
inline std::optional<std::vector<uint8_t>>
readOptBytes(lean_object* o) {
    return isSome(o) ? std::optional<std::vector<uint8_t>>(readBytes(optionVal(o))) : std::nullopt;
}

// Build an owned Lean `List` from values convertible via W::build.
template <class W, class C>
inline lean_object*
leanList(std::vector<C> const& xs)
{
    lean_object* lst = lean_box(0);
    for (auto it = xs.rbegin(); it != xs.rend(); ++it)
    {
        lean_object* cell = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cell, 0, W::build(*it).give());
        lean_ctor_set(cell, 1, lst);
        lst = cell;
    }
    return lst;
}

// Except String a: tag 0 = error (String @0), 1 = ok (value @0)
inline bool exceptOk(lean_object* e) { return lean_obj_tag(e) == 1; }
inline lean_object* exceptVal(lean_object* e) { return lean_ctor_get(e, 0); }

// An `Except String W` unpacked: the wrapped value on ok, the message on error.
template <class W>
struct LeanExcept
{
    std::optional<W> value;
    std::string error;
};
template <class W>
inline LeanExcept<W>
readExcept(lean_object* exceptOwned)
{
    LeanObjectFFI e(exceptOwned);
    if (exceptOk(e.raw()))
        return {W(retain(exceptVal(e.raw()))), {}};
    return {std::nullopt, lean_string_cstr(exceptVal(e.raw()))};
}
// Bool: false = tag 0, true = tag 1
inline bool leanBool(lean_object* b) { return lean_obj_tag(b) == 1; }
// Pair (a x b)
inline lean_object* pairFirst(lean_object* p) { return lean_ctor_get(p, 0); }
inline lean_object* pairSecond(lean_object* p) { return lean_ctor_get(p, 1); }

template <typename F>
inline void
leanForEach(lean_object* list, F&& f)
{
    for (lean_object* cur = list; lean_obj_tag(cur) == 1; cur = lean_ctor_get(cur, 1))
        f(lean_ctor_get(cur, 0));
}

inline void LeanObjectFFI::leanSetOptU32(lean_object* (*fn)(lean_object*, lean_object*), uint32_t v) { reset(fn(give(), leanSomeU32(v).give())); }
inline void LeanObjectFFI::leanSetOptU64(lean_object* (*fn)(lean_object*, lean_object*), uint64_t v) { reset(fn(give(), leanSomeU64(v).give())); }
inline void LeanObjectFFI::leanSetOptU8(lean_object* (*fn)(lean_object*, lean_object*), uint8_t v) { reset(fn(give(), leanSomeU8(v).give())); }
template <class Bytes>
inline void LeanObjectFFI::leanSetBytes(lean_object* (*fn)(lean_object*, lean_object*), Bytes const& v) { reset(fn(give(), mkBytes(v.data(), v.size()).give())); }
template <class Bytes>
inline void LeanObjectFFI::leanSetOptBytes(lean_object* (*fn)(lean_object*, lean_object*), Bytes const& v) { reset(fn(give(), leanSome(mkBytes(v.data(), v.size())).give())); }
template <class W, class C>
inline void LeanObjectFFI::leanSetOptHandle(lean_object* (*fn)(lean_object*, lean_object*), std::optional<C> const& v) { reset(fn(give(), leanOptHandle<W>(v))); }

}  // namespace lean4
}  // namespace test
}  // namespace xrpl
