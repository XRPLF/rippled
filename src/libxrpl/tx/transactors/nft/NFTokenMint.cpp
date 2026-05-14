/** @file
 *  Implementation of the NFTokenMint transactor.
 *
 *  Handles the full lifecycle of NFT creation: stateless field validation
 *  (`preflight`), read-only ledger checks (`preclaim`), and state mutations
 *  (`doApply`). Also provides `createNFTokenID`, the canonical algorithm for
 *  constructing the globally unique 256-bit NFToken identifier.
 */
#include <xrpl/tx/transactors/nft/NFTokenMint.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

#include <boost/endian/conversion.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>  // IWYU pragma: keep
#include <memory>
#include <utility>

namespace xrpl {

/** Extract the NFToken-flag half of the transaction flags word.
 *
 *  NFToken flags are packed into the lower 16 bits of the 32-bit transaction
 *  flags field. The upper 16 bits are reserved for transaction-level flags
 *  (e.g., `tfTransferable`, `tfTrustLine`). This helper isolates the token
 *  flags for storage in the NFToken ID and for passing to offer helpers.
 *
 *  @param txFlags  The raw 32-bit flags from `STTx::getFlags()`.
 *  @return The low 16 bits cast to `uint16_t`.
 */
static std::uint16_t
extractNFTokenFlagsFromTxFlags(std::uint32_t txFlags)
{
    return static_cast<std::uint16_t>(txFlags & 0x0000FFFF);
}

/** Return true if the transaction includes any embedded-offer fields.
 *
 *  The presence of `sfAmount`, `sfDestination`, or `sfExpiration` signals
 *  that the minter wants to create an initial sell offer atomically with the
 *  mint. Used by `checkExtraFeatures` to gate the sub-feature and by
 *  `preflight`/`preclaim` to decide whether to invoke the shared offer
 *  validation path.
 *
 *  @param ctx  Preflight context providing access to the transaction fields.
 *  @return `true` if at least one offer-related field is present.
 */
static bool
hasOfferFields(PreflightContext const& ctx)
{
    return ctx.tx.isFieldPresent(sfAmount) || ctx.tx.isFieldPresent(sfDestination) ||
        ctx.tx.isFieldPresent(sfExpiration);
}

bool
NFTokenMint::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureNFTokenMintOffer) || !hasOfferFields(ctx);
}

std::uint32_t
NFTokenMint::getFlagsMask(PreflightContext const& ctx)
{
    // Four combinations: {trustLine disabled, trustLine enabled} x {mutable URI, no mutable URI}
    std::uint32_t const nfTokenMintMask = [&]() -> std::uint32_t {
        if (ctx.rules.enabled(fixRemoveNFTokenAutoTrustLine))
        {
            return ctx.rules.enabled(featureDynamicNFT) ? tfNFTokenMintMask
                                                        : tfNFTokenMintMaskWithoutMutable;
        }
        return ctx.rules.enabled(featureDynamicNFT) ? tfNFTokenMintOldMaskWithMutable
                                                    : tfNFTokenMintOldMask;
    }();

    return nfTokenMintMask;
}

NotTEC
NFTokenMint::preflight(PreflightContext const& ctx)
{
    if (auto const f = ctx.tx[~sfTransferFee])
    {
        if (f > kMAX_TRANSFER_FEE)
            return temBAD_NFTOKEN_TRANSFER_FEE;

        // A transfer fee on a non-transferable token is contradictory:
        // there is no transfer that could ever collect it.
        if (f > 0u && !ctx.tx.isFlag(tfTransferable))
            return temMALFORMED;
    }

    if (auto iss = ctx.tx[~sfIssuer]; iss == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto uri = ctx.tx[~sfURI])
    {
        if (uri->empty() || uri->length() > kMAX_TOKEN_URI_LENGTH)
            return temMALFORMED;
    }

    if (hasOfferFields(ctx))
    {
        if (!ctx.tx.isFieldPresent(sfAmount))
            return temMALFORMED;

        // Pass tfSellNFToken because a mint-bundled offer is always a sell
        // offer; buy offers cannot be created atomically at mint time.
        if (NotTEC notTec = nft::tokenOfferCreatePreflight(
                ctx.tx[sfAccount],
                ctx.tx[sfAmount],
                ctx.tx[~sfDestination],
                ctx.tx[~sfExpiration],
                extractNFTokenFlagsFromTxFlags(ctx.tx.getFlags()),
                ctx.rules);
            !isTesSuccess(notTec))
        {
            return notTec;
        }
    }

    return tesSUCCESS;
}

uint256
NFTokenMint::createNFTokenID(
    std::uint16_t flags,
    std::uint16_t fee,
    AccountID const& issuer,
    nft::Taxon taxon,
    std::uint32_t tokenSeq)
{
    taxon = nft::cipheredTaxon(tokenSeq, taxon);

    flags = boost::endian::native_to_big(flags);
    fee = boost::endian::native_to_big(fee);
    taxon = nft::toTaxon(boost::endian::native_to_big(nft::toUInt32(taxon)));
    tokenSeq = boost::endian::native_to_big(tokenSeq);

    std::array<std::uint8_t, 32> buf{};

    auto ptr = buf.data();

    std::memcpy(ptr, &flags, sizeof(flags));
    ptr += sizeof(flags);

    std::memcpy(ptr, &fee, sizeof(fee));
    ptr += sizeof(fee);

    std::memcpy(ptr, issuer.data(), issuer.size());
    ptr += issuer.size();

    std::memcpy(ptr, &taxon, sizeof(taxon));
    ptr += sizeof(taxon);

    std::memcpy(ptr, &tokenSeq, sizeof(tokenSeq));
    ptr += sizeof(tokenSeq);
    XRPL_ASSERT(
        std::distance(buf.data(), ptr) == buf.size(),
        "xrpl::NFTokenMint::createNFTokenID : data size matches the buffer");

    return uint256::fromVoid(buf.data());
}

TER
NFTokenMint::preclaim(PreclaimContext const& ctx)
{
    if (auto issuer = ctx.tx[~sfIssuer])
    {
        auto const sle = ctx.view.read(keylet::account(*issuer));

        if (!sle)
            return tecNO_ISSUER;

        if (auto const minter = (*sle)[~sfNFTokenMinter]; minter != ctx.tx[sfAccount])
            return tecNO_PERMISSION;
    }

    if (ctx.tx.isFieldPresent(sfAmount))
    {
        if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
            return tecEXPIRED;

        // Pass tfSellNFToken: mint-bundled offers are always sell offers.
        if (TER const ter = nft::tokenOfferCreatePreclaim(
                ctx.view,
                ctx.tx[sfAccount],
                ctx.tx[~sfIssuer].value_or(ctx.tx[sfAccount]),
                ctx.tx[sfAmount],
                ctx.tx[~sfDestination],
                extractNFTokenFlagsFromTxFlags(ctx.tx.getFlags()),
                ctx.tx[~sfTransferFee].value_or(0),
                ctx.j);
            !isTesSuccess(ter))
            return ter;
    }
    return tesSUCCESS;
}

TER
NFTokenMint::doApply()
{
    auto const issuer = ctx_.tx[~sfIssuer].value_or(account_);

    auto const tokenSeq = [this, &issuer]() -> Expected<std::uint32_t, TER> {
        auto const root = view().peek(keylet::account(issuer));
        if (root == nullptr)
        {
            // Should not happen — checked in preclaim.
            return Unexpected(tecNO_ISSUER);  // LCOV_EXCL_LINE
        }

        // Bootstrap sfFirstNFTokenSequence on the very first mint.
        // By the time doApply runs, the account sequence has already been
        // pre-incremented for direct-sequence transactions, so we subtract 1
        // to recover the pre-tx value.  Ticket-based transactions do not
        // pre-increment the issuer's sequence, nor do authorized-minter
        // transactions (where the issuer is a different account entirely), so
        // in those two cases we use the sequence value as-is.
        if (!root->isFieldPresent(sfFirstNFTokenSequence))
        {
            std::uint32_t const acctSeq = root->at(sfSequence);

            root->at(sfFirstNFTokenSequence) =
                ctx_.tx.isFieldPresent(sfIssuer) || ctx_.tx.getSeqProxy().isTicket() ? acctSeq
                                                                                     : acctSeq - 1;
        }

        std::uint32_t const mintedNftCnt = (*root)[~sfMintedNFTokens].valueOr(0u);

        (*root)[sfMintedNFTokens] = mintedNftCnt + 1u;
        if ((*root)[sfMintedNFTokens] == 0u)
            return Unexpected(tecMAX_SEQUENCE_REACHED);

        // tokenSeq = sfFirstNFTokenSequence + sfMintedNFTokens (before increment)
        std::uint32_t const offset = (*root)[sfFirstNFTokenSequence];
        std::uint32_t const tokenSeq = offset + mintedNftCnt;

        if (tokenSeq + 1u == 0u || tokenSeq < offset)
            return Unexpected(tecMAX_SEQUENCE_REACHED);

        ctx_.view().update(root);
        return tokenSeq;
    }();

    if (!tokenSeq.has_value())
        return (tokenSeq.error());

    std::uint32_t const ownerCountBefore =
        view().read(keylet::account(account_))->getFieldU32(sfOwnerCount);

    SOTemplate const* nfTokenTemplate =
        InnerObjectFormats::getInstance().findSOTemplateBySField(sfNFToken);

    if (nfTokenTemplate == nullptr)
    {
        // Should never happen — sfNFToken is registered at startup.
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    auto const nftokenID = createNFTokenID(
        extractNFTokenFlagsFromTxFlags(ctx_.tx.getFlags()),
        ctx_.tx[~sfTransferFee].value_or(0),
        issuer,
        nft::toTaxon(ctx_.tx[sfNFTokenTaxon]),
        tokenSeq.value());

    STObject newToken(*nfTokenTemplate, sfNFToken, [this, &nftokenID](STObject& object) {
        object.setFieldH256(sfNFTokenID, nftokenID);

        if (auto const uri = ctx_.tx[~sfURI])
            object.setFieldVL(sfURI, *uri);
    });

    if (TER const ret = nft::insertToken(ctx_.view(), account_, std::move(newToken));
        !isTesSuccess(ret))
        return ret;

    if (ctx_.tx.isFieldPresent(sfAmount))
    {
        // Pass tfSellNFToken: mint-bundled offers are always sell offers.
        if (TER const ter = nft::tokenOfferCreateApply(
                view(),
                ctx_.tx[sfAccount],
                ctx_.tx[sfAmount],
                ctx_.tx[~sfDestination],
                ctx_.tx[~sfExpiration],
                ctx_.tx.getSeqProxy(),
                nftokenID,
                preFeeBalance_,
                j_);
            !isTesSuccess(ter))
            return ter;
    }

    // Reserve is only checked when the owner count increased (new page or sell
    // offer created).  Packing tokens into an existing page does not allocate a
    // new ledger object and therefore imposes no incremental reserve cost.
    if (auto const ownerCountAfter =
            view().read(keylet::account(account_))->getFieldU32(sfOwnerCount);
        ownerCountAfter > ownerCountBefore)
    {
        if (auto const reserve = view().fees().accountReserve(ownerCountAfter);
            preFeeBalance_ < reserve)
            return tecINSUFFICIENT_RESERVE;
    }
    return tesSUCCESS;
}

void
NFTokenMint::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
NFTokenMint::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
