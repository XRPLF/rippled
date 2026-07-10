#include <xrpl/ledger/helpers/SponsorHelpers.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/OracleHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <expected>
#include <optional>

namespace xrpl {

std::optional<AccountID>
getLedgerEntryOwner(ReadView const& view, SLE const& sle, AccountID const& account)
{
    switch (sle.getType())
    {
        case ltCHECK:
        case ltESCROW:
        case ltPAYCHAN:
        case ltMPTOKEN:
        case ltDELEGATE:
        case ltDEPOSIT_PREAUTH:
            return sle.getAccountID(sfAccount);
        case ltMPTOKEN_ISSUANCE:
            return sle.getAccountID(sfIssuer);
        case ltSIGNER_LIST: {
            SignerListEntry<ReadView> signerList{keylet::signerList(account), view};
            if (!signerList)
                return std::nullopt;
            if (signerList->key() == sle.key())
                return account;
            return std::nullopt;
        }
        case ltCREDENTIAL: {
            if (sle.isFlag(lsfAccepted))
                return sle.getAccountID(sfSubject);
            return sle.getAccountID(sfIssuer);
        }
        case ltRIPPLE_STATE: {
            if (sle.isFlag(lsfHighReserve))
            {
                auto const highAccount = sle.getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == account)
                    return highAccount;
            }
            if (sle.isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == account)
                    return lowAccount;
            }
            return std::nullopt;
        }
        default:
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::getLedgerEntryOwner : object is not supported by sponsorship.");
            return std::nullopt;
            // LCOV_EXCL_STOP
    };
}

std::uint32_t
getLedgerEntryOwnerCount(SLE const& sle)
{
    switch (sle.getType())
    {
        case ltORACLE: {
            return calculateOracleReserve(sle.getFieldArray(sfPriceDataSeries));
        }
        // Vaults require 2 owner counts (the vault and a pseudo-account)
        case ltVAULT:
            return 2;
        case ltSIGNER_LIST: {
            // Mirror SignerListSet's owner-count accounting so that create and
            // delete agree. Modern lists (post-MultiSignReserve) carry the
            // lsfOneOwnerCount flag and cost a single owner count. Legacy
            // pre-MultiSignReserve lists cost 2 + signer_count owner counts
            if (sle.isFlag(lsfOneOwnerCount))
                return 1;
            return 2 + static_cast<std::uint32_t>(sle.getFieldArray(sfSignerEntries).size());
        }
        case ltACCOUNT_ROOT:
            UNREACHABLE("AccountRoots are not supported by object sponsorship.");
            return 0;
        default:
            return 1;
    }
}

}  // namespace xrpl
