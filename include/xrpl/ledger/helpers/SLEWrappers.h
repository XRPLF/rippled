#pragma once

#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

/**
 * Concrete, view-parameterized wrappers for each ledger entry type.
 *
 * Every wrapper derives from SLEBase<ViewT> and, for now, does nothing more
 * than translate the entry's "keylet parts" (the arguments to its keylet::
 * function) into a Keylet that the base class resolves against the view:
 *
 *   AccountRootEntry<ApplyView> acct{id, view};     // peek (writable)
 *   AccountRootEntry<ReadView>  acct{id, readView}; // read  (read-only)
 *
 * Domain-specific accessors will be layered onto each wrapper over time.
 */

// Ordered to match include/xrpl/protocol/detail/ledger_entries.macro.

template <typename ViewT>
class NFTokenOfferEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NFTokenOfferEntry(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenOffer(owner, seq), view, j)
    {
    }
};

template <typename ViewT>
class CheckEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit CheckEntry(
        AccountID const& id,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::check(id, seq), view, j)
    {
    }
};

template <typename ViewT>
class DIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DIDEntry(
        AccountID const& account,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::did(account), view, j)
    {
    }
};

template <typename ViewT>
class NegativeUNLEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NegativeUNLEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::negativeUNL(), view, j)
    {
    }
};

template <typename ViewT>
class NFTokenPageEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NFTokenPageEntry(
        Keylet const& page,
        uint256 const& token,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenPage(page, token), view, j)
    {
    }
};

template <typename ViewT>
class SignerListEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit SignerListEntry(
        AccountID const& account,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::signerList(account), view, j)
    {
    }
};

template <typename ViewT>
class TicketEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit TicketEntry(
        AccountID const& id,
        std::uint32_t ticketSeq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ticket(id, ticketSeq), view, j)
    {
    }
};

template <typename ViewT>
class AccountRootEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AccountRootEntry(
        AccountID const& id,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::account(id), view, j)
    {
    }
};

template <typename ViewT>
class DirectoryNodeEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DirectoryNodeEntry(
        AccountID const& id,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ownerDir(id), view, j)
    {
    }
};

template <typename ViewT>
class AmendmentsEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AmendmentsEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amendments(), view, j)
    {
    }
};

template <typename ViewT>
class LedgerHashesEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LedgerHashesEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::skip(), view, j)
    {
    }
};

template <typename ViewT>
class BridgeEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit BridgeEntry(
        STXChainBridge const& bridge,
        STXChainBridge::ChainType chainType,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::bridge(bridge, chainType), view, j)
    {
    }
};

template <typename ViewT>
class OfferEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit OfferEntry(
        AccountID const& id,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::offer(id, seq), view, j)
    {
    }
};

template <typename ViewT>
class DepositPreauthEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DepositPreauthEntry(
        AccountID const& owner,
        AccountID const& preauthorized,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::depositPreauth(owner, preauthorized), view, j)
    {
    }
};

template <typename ViewT>
class XChainOwnedClaimIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit XChainOwnedClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainClaimID(bridge, seq), view, j)
    {
    }
};

template <typename ViewT>
class RippleStateEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit RippleStateEntry(
        AccountID const& id0,
        AccountID const& id1,
        Currency const& currency,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::trustLine(id0, id1, currency), view, j)
    {
    }
};

template <typename ViewT>
class FeeSettingsEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit FeeSettingsEntry(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::feeSettings(), view, j)
    {
    }
};

template <typename ViewT>
class XChainOwnedCreateAccountClaimIDEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit XChainOwnedCreateAccountClaimIDEntry(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainCreateAccountClaimID(bridge, seq), view, j)
    {
    }
};

template <typename ViewT>
class EscrowEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit EscrowEntry(
        AccountID const& src,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::escrow(src, seq), view, j)
    {
    }
};

template <typename ViewT>
class PayChannelEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit PayChannelEntry(
        AccountID const& src,
        AccountID const& dst,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::payChannel(src, dst, seq), view, j)
    {
    }
};

template <typename ViewT>
class AMMEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AMMEntry(
        Asset const& issue1,
        Asset const& issue2,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amm(issue1, issue2), view, j)
    {
    }
};

template <typename ViewT>
class MPTokenIssuanceEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit MPTokenIssuanceEntry(
        std::uint32_t seq,
        AccountID const& issuer,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::mptokenIssuance(seq, issuer), view, j)
    {
    }
};

template <typename ViewT>
class MPTokenEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit MPTokenEntry(
        MPTID const& issuanceID,
        AccountID const& holder,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::mptoken(issuanceID, holder), view, j)
    {
    }
};

template <typename ViewT>
class OracleEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit OracleEntry(
        AccountID const& account,
        std::uint32_t documentID,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::oracle(account, documentID), view, j)
    {
    }
};

template <typename ViewT>
class CredentialEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit CredentialEntry(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credType,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::credential(subject, issuer, credType), view, j)
    {
    }
};

template <typename ViewT>
class PermissionedDomainEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit PermissionedDomainEntry(
        AccountID const& account,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::permissionedDomain(account, seq), view, j)
    {
    }
};

template <typename ViewT>
class DelegateEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DelegateEntry(
        AccountID const& account,
        AccountID const& authorizedAccount,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::delegate(account, authorizedAccount), view, j)
    {
    }
};

template <typename ViewT>
class VaultEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit VaultEntry(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::vault(owner, seq), view, j)
    {
    }
};

template <typename ViewT>
class LoanBrokerEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LoanBrokerEntry(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loanBroker(owner, seq), view, j)
    {
    }
};

template <typename ViewT>
class LoanEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LoanEntry(
        uint256 const& loanBrokerID,
        std::uint32_t loanSeq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loan(loanBrokerID, loanSeq), view, j)
    {
    }
};

}  // namespace xrpl
