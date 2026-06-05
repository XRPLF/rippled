#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

class ValidPermissionedDEX
{
    bool regularOffers_ = false;
    bool badHybridsOld_ = false;  // pre-fixSecurity3_1_3: missing field/domain or size > 1
    bool badHybrids_ = false;     // post-fixSecurity3_1_3: also catches size == 0 (size != 1)
    hash_set<uint256> domains_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
