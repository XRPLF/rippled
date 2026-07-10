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

    // Owner dir (counts toward the source's reserve) + destination tracking dir
    // (added only for a real, non-self check, matching CheckCreate).
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const owner = this->sle()->getAccountID(sfAccount);
        auto const dest = this->sle()->getAccountID(sfDestination);
        std::vector<OwnerDirLink> dirs{{owner, &sfOwnerNode, /*countsToward=*/true}};
        if (dest != owner)
            dirs.push_back({dest, &sfDestinationNode, /*countsToward=*/false});
        return dirs;
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

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
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

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
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

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfAccount;
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

    // Owner dir (counts toward the sender's reserve) plus tracking dirs: the
    // destination's (when not a self-send) and, for IOU escrows, the issuer's
    // (to track the locked balance). MPT escrows track the lock on the issuance
    // object instead, so they take no issuer dir. Mirrors EscrowCreate.
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const& sle = *this->sle();
        AccountID const account = sle.getAccountID(sfAccount);
        AccountID const dest = sle.getAccountID(sfDestination);
        STAmount const amount = sle.getFieldAmount(sfAmount);

        std::vector<OwnerDirLink> dirs;
        dirs.push_back({account, &sfOwnerNode, /*countsToward=*/true});
        if (dest != account)
            dirs.push_back({dest, &sfDestinationNode, /*countsToward=*/false});

        AccountID const issuer = amount.getIssuer();
        if (!isXRP(amount) && issuer != account && issuer != dest && !amount.holds<MPTIssue>())
            dirs.push_back({issuer, &sfIssuerNode, /*countsToward=*/false});
        return dirs;
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

    // Owner dir (counts toward the source's reserve) + destination tracking dir
    // (PaymentChannelCreate forbids dst == src, so both are always present).
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        return {
            {this->sle()->getAccountID(sfAccount), &sfOwnerNode, /*countsToward=*/true},
            {this->sle()->getAccountID(sfDestination), &sfDestinationNode, /*countsToward=*/false}};
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

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfIssuer;
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

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfOwner;
    }

    // An Oracle with more than five price-data pairs occupies two reserve slots.
    [[nodiscard]] std::uint32_t
    reserveCount() const override
    {
        return this->sle()->getFieldArray(sfPriceDataSeries).size() > 5 ? 2 : 1;
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

    // A credential lives in both the issuer's and subject's directories, but is
    // only counted against one owner's reserve at a time: the issuer holds it
    // until the subject accepts (lsfAccepted), after which the subject owns it.
    // A self-issued credential is always owned (and counted) by the issuer.
    // Mirrors CredentialCreate / credentials::deleteSLE.
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        auto const& sle = *this->sle();
        AccountID const issuer = sle.getAccountID(sfIssuer);
        AccountID const subject = sle.getAccountID(sfSubject);
        bool const accepted = sle.isFlag(lsfAccepted);

        std::vector<OwnerDirLink> dirs;
        dirs.push_back({issuer, &sfIssuerNode, /*countsToward=*/!accepted || subject == issuer});
        if (subject != issuer)
            dirs.push_back({subject, &sfSubjectNode, /*countsToward=*/accepted});
        return dirs;
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

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfOwner;
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

    // Owner dir (counts toward the delegator's reserve) + the authorized
    // account's dir (so AccountDelete can find inbound delegations; no count).
    [[nodiscard]] std::vector<OwnerDirLink>
    ownerDirs() const override
    {
        return {
            {this->sle()->getAccountID(sfAccount), &sfOwnerNode, /*countsToward=*/true},
            {this->sle()->getAccountID(sfAuthorize), &sfDestinationNode, /*countsToward=*/false}};
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
