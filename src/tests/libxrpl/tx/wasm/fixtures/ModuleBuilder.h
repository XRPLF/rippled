#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>

namespace xrpl::test {

// Modules built to a *byte size* rather than to a behaviour.
//
// Everything else in this tree writes WAT, which is the right default: it says what the
// contract does. These two say only how big it is, for the transactor's `bytecodeSizeLimit`
// screening, where the boundary cases sit five bytes apart (99'950 accepted, 99'955
// refused). Assembling text cannot hit a byte count on the nose, and the WAT for a
// hundred thousand `nop`s would be a ~500 KB string, so these emit the binary directly.
//
// Both produce a module that *passes preflight* when it is under the size limit: a real
// `escrow_finish` exported with type `() -> i32`. That is not incidental — screening
// checks the entry point's signature (`PreflightTest.EntryPointOfTheWrongTypeIsRefused`),
// so a module that got it wrong would be refused for that reason at every size and the
// sweep would measure nothing.

// A module of `instructionCount` `nop`s in a single function, doing nothing.
//
// All of them in one function, deliberately: there appears to be no per-function size limit
// below the module limit, so one function may occupy the whole module. `wasmparser` defines
// `MAX_WASM_FUNCTION_SIZE` = 128 KiB, which looks like such a limit, but a single body of a
// million instructions preflights clean — pinned by
// `BytecodeSize.ASingleFunctionBodyIsNotSeparatelyCapped`. Splitting the `nop`s across
// functions would imply a constraint that is not there, and would make the byte count the
// boundary tests depend on harder to predict.
//
// The returned module is a few dozen bytes larger than `instructionCount` (the sections
// around the code). Callers that care about an exact total should measure `.size()` rather
// than assume it.
Bytes
codeHeavyModule(std::uint32_t instructionCount);

// A module carrying `dataBytes` bytes in a data segment, with memory declared to fit.
//
// The size lands in a data section rather than a code section, so the two builders
// together separate "large because there is a lot to translate" from "large because there
// is a lot to copy".
Bytes
dataHeavyModule(std::uint32_t dataBytes);

}  // namespace xrpl::test
