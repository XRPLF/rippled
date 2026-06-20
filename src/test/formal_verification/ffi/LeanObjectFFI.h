#pragma once

#include <lean/lean.h>

#include <concepts>

namespace xrpl::test::formal_verification {

// Owner of a lean_object*, and the base for every FFI wrapper. Refcounting
// lives here: subclasses go through the typed helpers and never touch the
// handle directly. Because Lean @[export] functions consume their object
// arguments, pass borrow() to a read and give() to a write; use raw() only
// to inspect a handle, never as an argument to a Lean call.
class LeanObjectFFI
{
    lean_object* o_ = nullptr;

protected:
    // fn(self) or fn(self, args...).
    template <class R, class... A>
    R
    leanGet(R (*fn)(lean_object*, A...), A... args) const
    {
        return fn(borrow(), args...);
    }

    void
    reset(lean_object* o) noexcept
    {
        if (o_)
            lean_dec(o_);
        o_ = o;
    }

public:
    LeanObjectFFI() = default;
    explicit LeanObjectFFI(lean_object* o) noexcept : o_(o)
    {
    }
    LeanObjectFFI(LeanObjectFFI const&) = delete;
    LeanObjectFFI&
    operator=(LeanObjectFFI const&) = delete;
    LeanObjectFFI(LeanObjectFFI&& other) noexcept : o_(other.o_)
    {
        other.o_ = nullptr;
    }
    LeanObjectFFI&
    operator=(LeanObjectFFI&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.o_);
            other.o_ = nullptr;
        }
        return *this;
    }
    ~LeanObjectFFI()
    {
        reset(nullptr);
    }

    // borrow, NO inc (@& / inspect)
    lean_object*
    raw() const noexcept
    {
        return o_;
    }
    // inc, for an owned callee
    lean_object*
    borrow() const noexcept
    {
        lean_inc(o_);
        return o_;
    }
    // transfer ownership out
    lean_object*
    give() noexcept
    {
        auto t = o_;
        o_ = nullptr;
        return t;
    }
};

// Any per-type wrapper around a Lean handle.
template <typename W>
concept LeanEntry = std::derived_from<W, LeanObjectFFI>;

// A typed value wrapper: a LeanEntry that builds from / reads back its CppType.
template <typename W>
concept LeanWrapper = LeanEntry<W> && requires(W const& w, typename W::CppType const& v) {
    { W::build(v) } -> std::same_as<W>;
    { w.read() } -> std::same_as<typename W::CppType>;
};

}  // namespace xrpl::test::formal_verification
