#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>

#include <tuple>
#include <utility>

namespace xrpl {

/** State information when preflighting a tx. */
struct PreflightContext
{
public:
    std::reference_wrapper<ServiceRegistry> registry;
    STTx const& tx;
    Rules const rules;
    ApplyFlags flags;
    std::optional<uint256 const> parentBatchId;
    beast::Journal const j;

    PreflightContext(
        ServiceRegistry& registry,
        STTx const& tx,
        uint256 parentBatchId,
        Rules rules,
        ApplyFlags flags,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : registry(registry)
        , tx(tx)
        , rules(std::move(rules))
        , flags(flags)
        , parentBatchId(parentBatchId)
        , j(j)
    {
        XRPL_ASSERT((flags & TapBatch) == TapBatch, "Batch apply flag should be set");
    }

    PreflightContext(
        ServiceRegistry& registry,
        STTx const& tx,
        Rules rules,
        ApplyFlags flags,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : registry(registry), tx(tx), rules(std::move(rules)), flags(flags), j(j)
    {
        XRPL_ASSERT((flags & TapBatch) == 0, "Batch apply flag should not be set");
    }

    PreflightContext&
    operator=(PreflightContext const&) = delete;
};

/** State information when determining if a tx is likely to claim a fee. */
struct PreclaimContext
{
public:
    std::reference_wrapper<ServiceRegistry> registry;
    ReadView const& view;
    TER preflightResult;
    ApplyFlags flags;
    STTx const& tx;
    std::optional<uint256 const> const parentBatchId;
    beast::Journal const j;

    PreclaimContext(
        ServiceRegistry& registry,
        ReadView const& view,
        TER preflightResult,
        STTx const& tx,
        ApplyFlags flags,
        std::optional<uint256> parentBatchId,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : registry(registry)
        , view(view)
        , preflightResult(preflightResult)
        , flags(flags)
        , tx(tx)
        , parentBatchId(parentBatchId)
        , j(j)
    {
        XRPL_ASSERT(
            parentBatchId.has_value() == ((flags & TapBatch) == TapBatch),
            "Parent Batch ID should be set if batch apply flag is set");
    }

    PreclaimContext(
        ServiceRegistry& registry,
        ReadView const& view,
        TER preflightResult,
        STTx const& tx,
        ApplyFlags flags,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : PreclaimContext(registry, view, preflightResult, tx, flags, std::nullopt, j)
    {
        XRPL_ASSERT((flags & TapBatch) == 0, "Batch apply flag should not be set");
    }

    PreclaimContext&
    operator=(PreclaimContext const&) = delete;
};

class TxConsequences;
struct PreflightResult;
// Needed for preflight specialization
class Change;

class Transactor
{
protected:
    ApplyContext& ctx_;
    beast::WrappedSink sink_;
    beast::Journal const j_;

    AccountID const accountID_;
    XRPAmount preFeeBalance_{};  // Balance before fees.

public:
    virtual ~Transactor() = default;
    Transactor(Transactor const&) = delete;
    Transactor&
    operator=(Transactor const&) = delete;

    enum class ConsequencesFactoryType { Normal, Blocker, Custom };

    /** Process the transaction. */
    ApplyResult
    operator()();

    ApplyView&
    view()
    {
        return ctx_.view();
    }

    [[nodiscard]] ApplyView const&
    view() const
    {
        return ctx_.view();
    }

    /** Check all invariants for the current transaction.
     *
     *  Runs transaction-specific invariants first (visitInvariantEntry +
     *  finalizeInvariants), then protocol-level invariants.  Both layers
     *  always run; the worst failure code is returned.
     *
     *  @param result  the tentative TER from transaction processing.
     *  @param fee     the fee consumed by the transaction.
     *
     *  @return the final TER after all invariant checks.
     */
    [[nodiscard]] TER
    checkInvariants(TER result, XRPAmount fee);

    /////////////////////////////////////////////////////
    /*
    These static functions are called from invoke_preclaim<Tx>
    using name hiding to accomplish compile-time polymorphism,
    so derived classes can override for different or extra
    functionality. Use with care, as these are not really
    virtual and so don't have the compiler-time protection that
    comes with it.
    */

    static NotTEC
    checkSeqProxy(ReadView const& view, STTx const& tx, beast::Journal j);

    static NotTEC
    checkPriorTxAndLastLedger(PreclaimContext const& ctx);

    static TER
    checkFee(PreclaimContext const& ctx, XRPAmount baseFee);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    static NotTEC
    checkBatchSign(PreclaimContext const& ctx);

    // Returns the fee in fee units, not scaled for load.
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /* Do NOT define an invokePreflight function in a derived class.
       Instead, define:

        // Optional if the transaction is gated on an amendment that
        // isn't specified in transactions.macro
        static bool
        checkExtraFeatures(PreflightContext const& ctx);

        // Optional if the transaction uses any flags other than tfUniversal
        static std::uint32_t
        getFlagsMask(PreflightContext const& ctx);

        // Required, even if it just returns tesSUCCESS.
        static NotTEC
        preflight(PreflightContext const& ctx);

        // Optional, rarely needed, if the transaction does any expensive
        // checks after the signature is verified.
        static NotTEC preflightSigValidated(PreflightContext const& ctx);

       * Do not try to call preflight1 or preflight2 directly.
       * Do not check whether relevant amendments are enabled in preflight.
         Instead, define checkExtraFeatures.
       * Do not check flags in preflight. Instead, define getFlagsMask.
    */
    template <class T>
    static NotTEC
    invokePreflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx)
    {
        // Most transactors do nothing
        // after checkSeq/Fee/Sign.
        return tesSUCCESS;
    }

    /**
     * This function can be overridden to introduce additional semantic constraints beyond the
     * granular template validation for granular permissions. It is called by the base
     * invokeCheckPermission method only after the transaction has successfully passed
     * checkGranularSandbox.
     */
    static NotTEC
    checkGranularSemantics(
        ReadView const& view,
        STTx const& tx,
        std::unordered_set<GranularPermissionType> const& heldGranularPermissions)
    {
        return tesSUCCESS;
    }

    /**
     * Checks whether the transaction is authorized to be executed by the delegated account.
     * This function enforces the strict permission check hierarchy. It is explicitly
     * designed NOT to be overridden. Derived transactors must instead implement
     * checkGranularSemantics to add custom validation logic for granular permissions.
     *
     * The evaluation proceeds as follows:
     * - If transaction-level permission is granted, the function immediately returns tesSUCCESS.
     * - If transaction-level permission is not granted, the function checks whether the transaction
     * matches the granular permission template defined in permissions.macro. If it does, it then
     * calls checkGranularSemantics to perform any additional, fine-grained validation.
     *
     */
    template <class T>
    static NotTEC
    invokeCheckPermission(ReadView const& view, STTx const& tx)
    {
        // heldGranularPermissions is passed by reference into checkPermission.
        // It is populated with the sender’s granular permissions only when the sender
        // lacks tx-level permission but has granular permissions that satisfy the
        // granular permission template.
        //
        // - result is terNO_DELEGATE_PERMISSION: return immediately.
        // - result is tesSUCCESS and heldGranularPermissions is empty: tx-level permission was
        // granted, so we returned success before populating it.
        // - result is tesSUCCESS and heldGranularPermissions is not empty: tx-level permission was
        // not granted, but the held granular permissions passed checkGranularSandbox, so we proceed
        // to checkGranularSemantics.
        //
        // WARNING: Do not simplify checkPermission to return only
        // heldGranularPermissions or the ter code. Both the result and the
        // populated set are required to enforce the strict permission hierarchy
        // described above.
        std::unordered_set<GranularPermissionType> heldGranularPermissions;
        if (NotTEC const result = checkPermission(view, tx, heldGranularPermissions);
            !isTesSuccess(result) || heldGranularPermissions.empty())
        {
            return result;
        }

        return T::checkGranularSemantics(view, tx, heldGranularPermissions);
    }
    /////////////////////////////////////////////////////

    // Interface used by AccountDelete
    static TER
    ticketDelete(
        ApplyView& view,
        AccountID const& account,
        uint256 const& ticketIndex,
        beast::Journal j);

protected:
    TER
    apply();

    explicit Transactor(ApplyContext& ctx);

    virtual void
    preCompute();

    virtual TER
    doApply() = 0;

    /** Inspect a single ledger entry modified by this transaction.
     *
     *  Called once for every SLE created, modified, or deleted by the
     *  transaction, before finalizeInvariants.  Implementations should
     *  accumulate whatever state they need to verify transaction-specific
     *  post-conditions.
     *
     *  @param isDelete  true if the entry was erased from the ledger.
     *  @param before    the entry's state before the transaction (nullptr
     *                   for newly created entries).
     *  @param after     the entry's state as supplied by the apply logic
     *                   for this transaction. For deletions, this is the
     *                   SLE being erased and is not guaranteed to be null;
     *                   callers must use isDelete rather than after == nullptr
     *                   to detect deletions.
     */
    virtual void
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) = 0;

    /** Check transaction-specific post-conditions after all entries have
     *  been visited.
     *
     *  Called once after every modified ledger entry has been passed to
     *  visitInvariantEntry.  Returns true if all transaction-specific
     *  invariants hold, or false to fail the transaction with
     *  tecINVARIANT_FAILED.
     *
     *  @param tx    the transaction being applied.
     *  @param result the tentative TER result so far.
     *  @param fee   the fee consumed by the transaction.
     *  @param view  read-only view of the ledger after the transaction.
     *  @param j     journal for logging invariant failures.
     *
     *  @return true if all invariants pass; false otherwise.
     */
    [[nodiscard]] virtual bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) = 0;

    /** Compute the minimum fee required to process a transaction
        with a given baseFee based on the current server load.

        @param registry The service registry.
        @param baseFee The base fee of a candidate transaction
            @see xrpl::calculateBaseFee
        @param fees Fee settings from the current ledger
        @param flags Transaction processing fees
     */
    static XRPAmount
    minimumFee(ServiceRegistry& registry, XRPAmount baseFee, Fees const& fees, ApplyFlags flags);

    // Returns the fee in fee units, not scaled for load.
    static XRPAmount
    calculateOwnerReserveFee(ReadView const& view, STTx const& tx);

    static NotTEC
    checkSign(
        ReadView const& view,
        ApplyFlags flags,
        std::optional<uint256 const> const& parentBatchId,
        AccountID const& idAccount,
        STObject const& sigObject,
        beast::Journal const j);

    // Base class always returns true
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    // Base class always returns tfUniversalMask
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    // Base class always returns tesSUCCESS
    static NotTEC
    preflightSigValidated(PreflightContext const& ctx);

    static bool
    validDataLength(std::optional<Slice> const& slice, std::size_t maxLength);

    template <class T>
    static bool
    validNumericRange(std::optional<T> value, T max, T min = T{});

    template <class T, class Unit>
    static bool
    validNumericRange(
        std::optional<T> value,
        unit::ValueUnit<Unit, T> max,
        unit::ValueUnit<Unit, T> min = unit::ValueUnit<Unit, T>{});

    /// Minimum will usually be zero.
    template <class T>
    static bool
    validNumericMinimum(std::optional<T> value, T min = T{});

    /// Minimum will usually be zero.
    template <class T, class Unit>
    static bool
    validNumericMinimum(
        std::optional<T> value,
        unit::ValueUnit<Unit, T> min = unit::ValueUnit<Unit, T>{});

private:
    static NotTEC
    checkPermission(
        ReadView const& view,
        STTx const& tx,
        std::unordered_set<GranularPermissionType>& heldGranularPermissions);

    std::pair<TER, XRPAmount>
    reset(XRPAmount fee);

    TER
    consumeSeqProxy(SLE::pointer const& sleAccount);

    TER
    payFee();

    std::tuple<TER, XRPAmount, bool>
    processPersistentChanges(TER result, XRPAmount fee);

    static NotTEC
    checkSingleSign(
        ReadView const& view,
        AccountID const& idSigner,
        AccountID const& idAccount,
        SLE::const_pointer sleAccount,
        beast::Journal const j);

    static NotTEC
    checkMultiSign(
        ReadView const& view,
        ApplyFlags flags,
        AccountID const& id,
        STObject const& sigObject,
        beast::Journal const j);

    void trapTransaction(uint256) const;

    /** Performs early sanity checks on the account and fee fields.

        (And passes flagMask to preflight0)

        Do not try to call preflight1 from preflight() in derived classes. See
        the description of invokePreflight for details.
    */
    static NotTEC
    preflight1(PreflightContext const& ctx, std::uint32_t flagMask);

    /** Checks whether the signature appears valid

        Do not try to call preflight2 from preflight() in derived classes. See
        the description of invokePreflight for details.
    */
    static NotTEC
    preflight2(PreflightContext const& ctx);

    /** Universal validations
       - Valid MPTAmount and XRPAmount

        Do not try to call preflightUniversal from preflight() in derived classes. See
        the description of invokePreflight for details.
    */
    static NotTEC
    preflightUniversal(PreflightContext const& ctx);

    /** Check transaction-specific invariants only.
     *
     *  Walks every modified ledger entry via visitInvariantEntry, then
     *  calls finalizeInvariants on the derived transactor.  Returns
     *  tecINVARIANT_FAILED if any transaction invariant is violated.
     *
     *  @param result  the tentative TER from transaction processing.
     *  @param fee     the fee consumed by the transaction.
     *
     *  @return the original result if all invariants pass, or
     *          tecINVARIANT_FAILED otherwise.
     */
    [[nodiscard]] TER
    checkTransactionInvariants(TER result, XRPAmount fee);
};

inline bool
Transactor::checkExtraFeatures(PreflightContext const& ctx)
{
    return true;
}

/** Performs early sanity checks on the txid and flags */
NotTEC
preflight0(PreflightContext const& ctx, std::uint32_t flagMask);

namespace detail {

/** Checks the validity of the transactor signing key.
 *
 * Normally called from preflight1 with ctx.tx.
 */
NotTEC
preflightCheckSigningKey(STObject const& sigObject, beast::Journal j);

/** Checks the special signing key state needed for simulation
 *
 * Normally called from preflight2 with ctx.tx.
 */
std::optional<NotTEC>
preflightCheckSimulateKeys(ApplyFlags flags, STObject const& sigObject, beast::Journal j);
}  // namespace detail

// Defined in Change.cpp
template <>
NotTEC
Transactor::invokePreflight<Change>(PreflightContext const& ctx);

template <class T>
NotTEC
Transactor::invokePreflight(PreflightContext const& ctx)
{
    // Using this lookup does NOT require checking the fixDelegateV1_1. The data
    // exists regardless of whether it is enabled.
    auto const feature = Permission::getInstance().getTxFeature(ctx.tx.getTxnType());

    if (feature && !ctx.rules.enabled(*feature))
        return temDISABLED;

    if (!T::checkExtraFeatures(ctx))
        return temDISABLED;

    if (auto const ret = preflight1(ctx, T::getFlagsMask(ctx)))
        return ret;

    if (auto const ret = preflightUniversal(ctx))
        return ret;

    if (auto const ret = T::preflight(ctx))
        return ret;

    if (auto const ret = preflight2(ctx))
        return ret;

    return T::preflightSigValidated(ctx);
}

template <class T>
bool
Transactor::validNumericRange(std::optional<T> value, T max, T min)
{
    if (!value)
        return true;
    return value >= min && value <= max;
}

template <class T, class Unit>
bool
Transactor::validNumericRange(
    std::optional<T> value,
    unit::ValueUnit<Unit, T> max,
    unit::ValueUnit<Unit, T> min)
{
    return validNumericRange(value, max.value(), min.value());
}

template <class T>
bool
Transactor::validNumericMinimum(std::optional<T> value, T min)
{
    if (!value)
        return true;
    return value >= min;
}

template <class T, class Unit>
bool
Transactor::validNumericMinimum(std::optional<T> value, unit::ValueUnit<Unit, T> min)
{
    return validNumericMinimum(value, min.value());
}

}  // namespace xrpl
