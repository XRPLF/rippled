#include <atomic>
#include <cstddef>
#include <iostream>

// Regression test: the compiler-rt sanitizer interface headers must be on the
// include path. A bare on-PATH clang in the Nix CI env doesn't get them
// propagated automatically, so this include would fail to compile with clang++
// if the env isn't wired up correctly. abseil hits the same include during
// sanitizer builds. LeakSanitizer ships with AddressSanitizer.
#include <sanitizer/lsan_interface.h>

#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
int
read_after_free(volatile int* array, std::size_t index)
{
    std::atomic_signal_fence(std::memory_order_seq_cst);
    int value = array[index];
    std::atomic_signal_fence(std::memory_order_seq_cst);
    return value;
}

int
main()
{
    int* array = new int[5]{10, 20, 30, 40, 50};
    delete[] array;

    std::cout << "Value at index 2: " << read_after_free(array, 2) << std::endl;

    return 0;
}
