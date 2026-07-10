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
 *   AccountRoot<ApplyView> acct{id, view};     // peek (writable)
 *   AccountRoot<ReadView>  acct{id, readView}; // read  (read-only)
 *
 * Domain-specific accessors will be layered onto each wrapper over time.
 */

// Ordered to match include/xrpl/protocol/detail/ledger_entries.macro.

template <typename ViewT>
class NFTokenOffer : public SLEBase<ViewT>
{
public:
    explicit NFTokenOffer(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenOffer(owner, seq), view, j)
    {
    }
};

template <typename ViewT>
class Check : public SLEBase<ViewT>
{
public:
    explicit Check(
        AccountID const& id,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::check(id, seq), view, j)
    {
    }
};

template <typename ViewT>
class DID : public SLEBase<ViewT>
{
public:
    explicit DID(
        AccountID const& account,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::did(account), view, j)
    {
    }
};

template <typename ViewT>
class NegativeUNL : public SLEBase<ViewT>
{
public:
    explicit NegativeUNL(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::negativeUNL(), view, j)
    {
    }
};

template <typename ViewT>
class NFTokenPage : public SLEBase<ViewT>
{
public:
    explicit NFTokenPage(
        Keylet const& page,
        uint256 const& token,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenPage(page, token), view, j)
    {
    }
};

template <typename ViewT>
class SignerList : public SLEBase<ViewT>
{
public:
    explicit SignerList(
        AccountID const& account,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::signerList(account), view, j)
    {
    }
};

template <typename ViewT>
class Ticket : public SLEBase<ViewT>
{
public:
    explicit Ticket(
        AccountID const& id,
        std::uint32_t ticketSeq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ticket(id, ticketSeq), view, j)
    {
    }
};

template <typename ViewT>
class AccountRoot : public SLEBase<ViewT>
{
public:
    explicit AccountRoot(
        AccountID const& id,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::account(id), view, j)
    {
    }
};

template <typename ViewT>
class DirectoryNode : public SLEBase<ViewT>
{
public:
    explicit DirectoryNode(
        AccountID const& id,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ownerDir(id), view, j)
    {
    }
};

template <typename ViewT>
class Amendments : public SLEBase<ViewT>
{
public:
    explicit Amendments(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amendments(), view, j)
    {
    }
};

template <typename ViewT>
class LedgerHashes : public SLEBase<ViewT>
{
public:
    explicit LedgerHashes(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::skip(), view, j)
    {
    }
};

template <typename ViewT>
class Bridge : public SLEBase<ViewT>
{
public:
    explicit Bridge(
        STXChainBridge const& bridge,
        STXChainBridge::ChainType chainType,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::bridge(bridge, chainType), view, j)
    {
    }
};

template <typename ViewT>
class Offer : public SLEBase<ViewT>
{
public:
    explicit Offer(
        AccountID const& id,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::offer(id, seq), view, j)
    {
    }
};

template <typename ViewT>
class DepositPreauth : public SLEBase<ViewT>
{
public:
    explicit DepositPreauth(
        AccountID const& owner,
        AccountID const& preauthorized,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::depositPreauth(owner, preauthorized), view, j)
    {
    }
};

template <typename ViewT>
class XChainOwnedClaimID : public SLEBase<ViewT>
{
public:
    explicit XChainOwnedClaimID(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainClaimID(bridge, seq), view, j)
    {
    }
};

template <typename ViewT>
class RippleState : public SLEBase<ViewT>
{
public:
    explicit RippleState(
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
class FeeSettings : public SLEBase<ViewT>
{
public:
    explicit FeeSettings(
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::feeSettings(), view, j)
    {
    }
};

template <typename ViewT>
class XChainOwnedCreateAccountClaimID : public SLEBase<ViewT>
{
public:
    explicit XChainOwnedCreateAccountClaimID(
        STXChainBridge const& bridge,
        std::uint64_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::xChainCreateAccountClaimID(bridge, seq), view, j)
    {
    }
};

template <typename ViewT>
class Escrow : public SLEBase<ViewT>
{
public:
    explicit Escrow(
        AccountID const& src,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::escrow(src, seq), view, j)
    {
    }
};

template <typename ViewT>
class PayChannel : public SLEBase<ViewT>
{
public:
    explicit PayChannel(
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
class AMM : public SLEBase<ViewT>
{
public:
    explicit AMM(
        Asset const& issue1,
        Asset const& issue2,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amm(issue1, issue2), view, j)
    {
    }
};

template <typename ViewT>
class MPTokenIssuance : public SLEBase<ViewT>
{
public:
    explicit MPTokenIssuance(
        std::uint32_t seq,
        AccountID const& issuer,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::mptokenIssuance(seq, issuer), view, j)
    {
    }
};

template <typename ViewT>
class MPToken : public SLEBase<ViewT>
{
public:
    explicit MPToken(
        MPTID const& issuanceID,
        AccountID const& holder,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::mptoken(issuanceID, holder), view, j)
    {
    }
};

template <typename ViewT>
class Oracle : public SLEBase<ViewT>
{
public:
    explicit Oracle(
        AccountID const& account,
        std::uint32_t documentID,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::oracle(account, documentID), view, j)
    {
    }
};

template <typename ViewT>
class Credential : public SLEBase<ViewT>
{
public:
    explicit Credential(
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
class PermissionedDomain : public SLEBase<ViewT>
{
public:
    explicit PermissionedDomain(
        AccountID const& account,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::permissionedDomain(account, seq), view, j)
    {
    }
};

template <typename ViewT>
class Delegate : public SLEBase<ViewT>
{
public:
    explicit Delegate(
        AccountID const& account,
        AccountID const& authorizedAccount,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::delegate(account, authorizedAccount), view, j)
    {
    }
};

template <typename ViewT>
class Vault : public SLEBase<ViewT>
{
public:
    explicit Vault(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::vault(owner, seq), view, j)
    {
    }
};

template <typename ViewT>
class LoanBroker : public SLEBase<ViewT>
{
public:
    explicit LoanBroker(
        AccountID const& owner,
        std::uint32_t seq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loanBroker(owner, seq), view, j)
    {
    }
};

template <typename ViewT>
class Loan : public SLEBase<ViewT>
{
public:
    explicit Loan(
        uint256 const& loanBrokerID,
        std::uint32_t loanSeq,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loan(loanBrokerID, loanSeq), view, j)
    {
    }
};

template <typename ViewT>
class Sponsorship : public SLEBase<ViewT>
{
public:
    explicit Sponsorship(
        AccountID const& sponsor,
        AccountID const& sponsee,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::sponsorship(sponsor, sponsee), view, j)
    {
    }
};

}  // namespace xrpl
