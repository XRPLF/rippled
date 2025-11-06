#ifndef XRPL_TX_PAYMENT_H_INCLUDED
#define XRPL_TX_PAYMENT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class Payment : public Transactor
{
    /* The largest number of paths we allow */
    static std::size_t const MaxPathSize = 6;

    /* The longest path we allow */
    static std::size_t const MaxPathLength = 8;

public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit Payment(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    // Helpers for this class and other transactors that make "Payments"
    struct RipplePaymentParams
    {
        ApplyContext& ctx;
        STAmount const& maxSourceAmount;
        AccountID const& srcAccountID;
        AccountID const& dstAccountID;
        SLE::pointer sleDst;
        STAmount const& dstAmount;
        // Paths need to be explicitly included because other transactions don't
        // have them defined
        STPathSet const& paths;
        std::optional<STAmount> const& deliverMin;
        bool partialPaymentAllowed = false;
        bool defaultPathsAllowed = true;
        bool limitQuality = false;
        beast::Journal j;
    };

    static STAmount
    getMaxSourceAmount(
        AccountID const& senderAccount,
        STAmount const& dstAmount,
        std::optional<STAmount> const& sendMax = {});

    static TER
    makeRipplePayment(RipplePaymentParams const& p);

    static TER
    makeMPTDirectPayment(RipplePaymentParams const& p);
};

}  // namespace ripple

#endif
