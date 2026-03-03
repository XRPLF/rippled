#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

class ValidMPTIssuance
{
    std::uint32_t mptIssuancesCreated_ = 0;
    std::uint32_t mptIssuancesDeleted_ = 0;

    std::uint32_t mptokensCreated_ = 0;
    std::uint32_t mptokensDeleted_ = 0;
    // non-MPT transactions may attempt to create
    // MPToken by an issuer
    bool mptCreatedByIssuer_ = false;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
