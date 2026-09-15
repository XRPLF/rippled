#pragma once

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

class ValidPermissionedDEX
{
    bool regularOffersOld_ = false;  // pre-fixCleanup3_2_0: also flags deleted offers
    bool regularOffers_ = false;     // post-fixCleanup3_2_0: excludes deleted offers
    bool badHybridsOld_ = false;     // pre-fixCleanup3_1_3: missing field/domain or size > 1
    bool badHybrids_ = false;        // post-fixCleanup3_1_3: also catches size == 0 (size != 1)
    HashSet<UInt256> domainsOld_;    // pre-fixCleanup3_4_0: also flags deleted domains
    HashSet<UInt256> domains_;       // post-fixCleanup3_4_0: excludes deleted domains

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
