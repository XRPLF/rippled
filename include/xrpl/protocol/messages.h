#pragma once

/** @file
 *  Safe include shim for the XRPL peer-to-peer protobuf definitions.
 *
 *  All code that needs access to the XRPL wire-protocol types (the
 *  `protocol::MessageType` enum, `TMTransaction`, `TMProposeSet`,
 *  `TMValidation`, `TMSquelch`, and the rest of the peer message
 *  hierarchy) must include this header rather than
 *  `<xrpl/proto/xrpl.pb.h>` directly.  The indirection exists to
 *  resolve a macro conflict that would otherwise produce cryptic build
 *  failures on affected platforms.
 *
 *  Some versions of `protoc` emit C++ that uses `TYPE_BOOL` as a plain
 *  identifier.  Certain platform headers (notably parts of the Windows
 *  SDK) define `TYPE_BOOL` as a preprocessor macro, so when
 *  `xrpl.pb.h` is compiled in a translation unit where that macro is
 *  live the preprocessor expands it mid-compilation and corrupts the
 *  generated symbol names.  The `#ifdef` guard before the `#undef` is
 *  deliberate: it keeps the file a no-op on platforms that never define
 *  the macro, avoiding compilers that warn on `#undef` of an undefined
 *  name.
 *
 *  @note The `TYPE_BOOL` workaround is technical debt.  It should be
 *      removed once the project upgrades to a `protoc` version that no
 *      longer generates code conflicting with that macro name.
 *
 *  @see https://github.com/google/protobuf/issues/549
 */

// TODO: Remove this after the protoc we use is upgraded to not generate
//       code that conflicts with the TYPE_BOOL macro.
#ifdef TYPE_BOOL
#undef TYPE_BOOL
#endif

#include <xrpl/proto/xrpl.pb.h>
