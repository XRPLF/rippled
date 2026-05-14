/** @file
 *  Umbrella header for the XRPL Serialized Type (ST) system.
 *
 *  Including this header makes the complete ST type vocabulary available in a
 *  single line. Every type that can appear inside a transaction, ledger entry,
 *  or consensus message is reachable transitively:
 *
 *  - **Field registry** — `SField`, `SOTemplate`, `SOEStyle`, and the full set
 *    of named fields (`sfAmount`, `sfDestination`, …) via `SField.h`.
 *  - **Polymorphic root** — `STBase` and the `detail::STVar` small-buffer
 *    helper via `STBase.h`.
 *  - **Scalar leaves** — `STInteger<T>` / `STUInt8` … `STUInt64` / `STInt32`
 *    (`STInteger.h`); `STBitString<Bits>` / `STUInt128` … `STUInt256`
 *    (`STBitString.h`); `STBlob` (`STBlob.h`); `STAccount` (`STAccount.h`).
 *  - **Composite containers** — `STObject` with `SOTemplate` enforcement and
 *    typed proxy accessors (`STObject.h`); `STArray` of inner `STObject`
 *    values (`STArray.h`); `STVector256` for multi-hash fields (`STVector256.h`).
 *  - **Domain-specific types** — `STAmount` (XRP drops or IOU amount),
 *    `STNumber` (high-precision asset-contextual value), `STPathSet` (payment
 *    path graphs), `STIssue`, `STCurrency`.
 *  - **High-level protocol objects** — `STTx` (signed transaction with
 *    cached ID and type), `STLedgerEntry` / `SLE` (keyed ledger state entry),
 *    `STValidation` (consensus vote with lazy signature cache).
 *  - **JSON boundary** — `STParsedJSON` for deserializing a `Json::Value` tree
 *    into an `STObject` or `STArray`.
 *
 *  @note Code that works with only one or two ST primitives should prefer the
 *      individual headers to keep compile-time dependencies minimal. Include
 *      `st.h` when a translation unit genuinely needs the full protocol-object
 *      vocabulary (transaction processing, serialization, RPC formatting, etc.).
 *
 *  @see STBase.h, STObject.h, STTx.h, STLedgerEntry.h, STValidation.h
 */
#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/STVector256.h>
