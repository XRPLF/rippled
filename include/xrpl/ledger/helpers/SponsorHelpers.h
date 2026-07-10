#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <unordered_set>

namespace xrpl {

std::optional<AccountID>
getLedgerEntryOwner(ReadView const& view, SLE const& sle, AccountID const& account);

std::uint32_t
getLedgerEntryOwnerCount(SLE const& sle);

}  // namespace xrpl
