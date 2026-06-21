#pragma once

#include <xrpl/beast/unit_test.h>

#include <lean/lean.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <random>
#include <string_view>

extern "C" void
lean_initialize_runtime_module(void);

extern "C" lean_object*
initialize_XRPL_XRPL_FFI_FFI(uint8_t builtin, lean_object* w);

namespace xrpl::test::formal_verification {

// Per-thread RNG; beginCase reseeds it deterministically per case.
inline std::mt19937_64&
nextRng()
{
    thread_local std::mt19937_64 rng{0xBEEFCAFEDEADBEEFULL};
    return rng;
}

// Base for Lean-FFI test suites. run() guards Lean init + serializes all FFI
// calls behind a mutex (Lean's runtime is single-threaded).
class LeanSuite : public beast::unit_test::Suite
{
    static std::mutex&
    leanMutex()
    {
        static std::mutex m;
        return m;
    }

    static bool
    ensureLeanInit()
    {
        static std::once_flag flag;
        static bool ok = false;
        std::call_once(flag, [] {
            lean_initialize_runtime_module();
            lean_object* res = initialize_XRPL_XRPL_FFI_FFI(1, lean_io_mk_world());
            if (!lean_io_result_is_ok(res))
            {
                lean_dec(res);
                return;
            }
            lean_dec_ref(res);
            lean_io_mark_end_initialization();
            ok = true;
        });
        return ok;
    }

    virtual void
    runTests() = 0;

protected:
    // Pass "Suite.method" for seedRng so the per-thread RNG seed is unique
    // across suites that share method names.
    void
    beginCase(char const* name, bool seedRng = false)
    {
        testcase << name;
        if (seedRng)
            nextRng().seed(std::hash<std::string_view>{}(name));
    }

    template <typename F>
    void
    runFuzz(int iterations, F&& check)
    {
        for (int i = 0; i < iterations; ++i)
            (void)check();
    }

public:
    void
    run() final
    {
        std::lock_guard<std::mutex> lock(leanMutex());
        if (!ensureLeanInit())
        {
            fail("Lean runtime failed to initialize");
            return;
        }
        runTests();
    }
};

}  // namespace xrpl::test::formal_verification
