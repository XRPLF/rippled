#pragma once

/*
This header file enables code coverage instrumentation. It is distributed with the Antithesis C++ SDK.

This header file can be used in both C and C++ programs. (The rest of the SDK works only for C++ programs.)

You should include it in a single .cpp or .c file.

The instructions (such as required compiler flags) and usage guidance are found at https://antithesis.com/docs/using_antithesis/sdk/cpp/overview.html.
*/

#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#ifndef __cplusplus
#include <stdbool.h>
#include <stddef.h>
#endif

// If the libvoidstar(determ) library is present, 
// pass thru trace_pc_guard related callbacks to it
typedef void (*trace_pc_guard_init_fn)(uint32_t *start, uint32_t *stop);
typedef void (*trace_pc_guard_fn)(uint32_t *guard, uint64_t edge);

static trace_pc_guard_init_fn trace_pc_guard_init = NULL;
static trace_pc_guard_fn trace_pc_guard = NULL;
static bool did_check_libvoidstar = false;
static bool has_libvoidstar = false;

static __attribute__((no_sanitize("coverage"))) void debug_message_out(const char *msg) {
  (void)printf("%s\n", msg);
  return;
}

extern
#ifdef __cplusplus
    "C"
#endif
__attribute__((no_sanitize("coverage"))) void antithesis_load_libvoidstar() {
#ifdef __cplusplus
    constexpr
#endif
    const char* LIB_PATH = "/usr/lib/libvoidstar.so";

    if (did_check_libvoidstar) {
      return;
    }
    debug_message_out("TRYING TO LOAD libvoidstar");
    did_check_libvoidstar = true;
    void* shared_lib = dlopen(LIB_PATH, RTLD_NOW);
    if (!shared_lib) {
        debug_message_out("Can not load the Antithesis native library");
        return;
    }

    void* trace_pc_guard_init_sym = dlsym(shared_lib, "__sanitizer_cov_trace_pc_guard_init");
    if (!trace_pc_guard_init_sym) {
        debug_message_out("Can not forward calls to libvoidstar for __sanitizer_cov_trace_pc_guard_init");
        return;
    }

    void* trace_pc_guard_sym = dlsym(shared_lib, "__sanitizer_cov_trace_pc_guard_internal");
    if (!trace_pc_guard_sym) {
        debug_message_out("Can not forward calls to libvoidstar for __sanitizer_cov_trace_pc_guard");
        return;
    }

    trace_pc_guard_init = (trace_pc_guard_init_fn)(trace_pc_guard_init_sym);
    trace_pc_guard = (trace_pc_guard_fn)(trace_pc_guard_sym);
    has_libvoidstar = true;
    debug_message_out("LOADED libvoidstar");
}

// The following symbols are indeed reserved identifiers, since we're implementing functions defined
// in the compiler runtime. Not clear how to get Clang on board with that besides narrowly suppressing
// the warning in this case. The sample code on the CoverageSanitizer documentation page fails this 
// warning!
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
extern 
#ifdef __cplusplus
    "C"
#endif
void __sanitizer_cov_trace_pc_guard_init(uint32_t *start, uint32_t *stop) {
    debug_message_out("SDK forwarding to libvoidstar for __sanitizer_cov_trace_pc_guard_init()");
    if (!did_check_libvoidstar) {
        antithesis_load_libvoidstar();
    }
    if (has_libvoidstar) {
        trace_pc_guard_init(start, stop);
    }
    return;
}

extern 
#ifdef __cplusplus
    "C"
#endif
void __sanitizer_cov_trace_pc_guard( uint32_t *guard ) {
    if (has_libvoidstar) {
        uint64_t edge = (uint64_t)(__builtin_return_address(0));
        trace_pc_guard(guard, edge);
    } else {
        if (guard) {
          *guard = 0;
        }
    }
    return;
}
#pragma clang diagnostic pop
