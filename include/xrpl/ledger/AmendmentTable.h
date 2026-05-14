#pragma once

#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/shamap/SHAMap.h>

#include <optional>
#include <utility>

namespace xrpl {

class ServiceRegistry;

/** Tracks enabled and pending amendments and coordinates validator voting.
 *
 *  Each protocol change (amendment) must achieve an 80% supermajority of
 *  trusted validators for `majorityTime` before it activates. This class
 *  manages the full lifecycle: registration of supported amendments, vote
 *  aggregation across flag ledgers, pseudo-transaction injection at consensus
 *  time, and detection of "amendment blocked" conditions where the network has
 *  enabled a feature this node does not support.
 *
 *  The interface is split into two layers. The pure virtual methods form the
 *  internal API that the concrete implementation satisfies, operating on
 *  pre-extracted amendment sets. Two concrete non-virtual adapter methods
 *  (`doValidatedLedger(shared_ptr<ReadView>)` and
 *  `doVoting(shared_ptr<ReadView>, ...)`) read amendment state from a
 *  `ReadView` and delegate to the pure-virtual overloads, keeping the
 *  implementation independent of the ledger view layer.
 *
 *  @note Amendment voting is only meaningful at flag ledgers (multiples of
 *      256). Use `needValidatedLedger` to gate the more expensive
 *      `doValidatedLedger` call.
 *  @see Feature.h for `VoteBehavior` and `majorityAmendments_t`
 */
class AmendmentTable
{
public:
    /** Metadata for a single registered amendment.
     *
     *  Bundles the human-readable name, canonical 256-bit hash, and compiled-in
     *  vote preference for one amendment. Non-default-constructible: every
     *  instance must carry all three fields.
     *
     *  @note Amendments with `VoteBehavior::Obsolete` are still registered so
     *      the node remains amendment-unblocked if the network enables them, but
     *      the node will never emit votes for them and their vote behavior cannot
     *      be overridden by config.
     */
    struct FeatureInfo
    {
        FeatureInfo() = delete;

        /** Construct a FeatureInfo with all required fields.
         *
         *  @param n Human-readable amendment name (e.g., "OwnerPaysFee").
         *  @param f Canonical 256-bit amendment hash used in ledger state and
         *      validations.
         *  @param v Compiled-in voting preference (`DefaultYes`, `DefaultNo`,
         *      or `Obsolete`).
         */
        FeatureInfo(std::string n, uint256 const& f, VoteBehavior v)
            : name(std::move(n)), feature(f), vote(v)
        {
        }

        /** Human-readable name of the amendment. */
        std::string const name;

        /** Canonical 256-bit amendment identifier used throughout the ledger. */
        uint256 const feature;

        /** Compiled-in voting preference for this amendment. */
        VoteBehavior const vote;
    };

    virtual ~AmendmentTable() = default;

    /** Look up an amendment's 256-bit hash by its human-readable name.
     *
     *  @param name The amendment name to look up (case-sensitive).
     *  @return The amendment's `uint256` hash, or a zero value if no
     *      amendment with that name is registered.
     */
    [[nodiscard]] virtual uint256
    find(std::string const& name) const = 0;

    /** Suppress this node's vote for an amendment.
     *
     *  Changes the amendment's vote from Up to Down regardless of the
     *  compiled-in `VoteBehavior`. May be called on amendments not in the
     *  supported list; an entry is created if one does not exist. The new
     *  state is persisted to the wallet database.
     *
     *  @param amendment The 256-bit amendment hash to veto.
     *  @return `true` if the vote state changed (was Up, now Down);
     *      `false` if the amendment was already Down-voted or Obsolete.
     */
    virtual bool
    veto(uint256 const& amendment) = 0;

    /** Remove a previously applied veto for an amendment.
     *
     *  Reverts the amendment's vote from Down back to Up. The change is
     *  persisted to the wallet database. Has no effect if the amendment
     *  was never vetoed, does not exist, or has `VoteBehavior::Obsolete`
     *  (Obsolete amendments cannot be unvetoed).
     *
     *  @param amendment The 256-bit amendment hash to un-veto.
     *  @return `true` if the vote state changed (was Down, now Up);
     *      `false` if the amendment was not in the Down state.
     */
    virtual bool
    unVeto(uint256 const& amendment) = 0;

    /** Mark an amendment as enabled in the local amendment table.
     *
     *  Directly flips the amendment's enabled flag. Called by
     *  `doValidatedLedger` when ledger state confirms the amendment is active.
     *  If the amendment is not in the supported list, `hasUnsupportedEnabled()`
     *  will subsequently return `true`.
     *
     *  @param amendment The 256-bit amendment hash to enable.
     *  @return `true` if the amendment was not already enabled;
     *      `false` if it was already in the enabled state.
     */
    virtual bool
    enable(uint256 const& amendment) = 0;

    /** Return whether an amendment is currently active on the network.
     *
     *  @param amendment The 256-bit amendment hash to query.
     *  @return `true` if the amendment has been enabled via `enable()` or
     *      through ledger validation; `false` otherwise.
     */
    [[nodiscard]] virtual bool
    isEnabled(uint256 const& amendment) const = 0;

    /** Return whether this node's software knows about and supports an amendment.
     *
     *  @param amendment The 256-bit amendment hash to query.
     *  @return `true` if the amendment was included in the `supported` list
     *      passed to `makeAmendmentTable`; `false` for unknown amendments.
     */
    [[nodiscard]] virtual bool
    isSupported(uint256 const& amendment) const = 0;

    /** Return whether any network-enabled amendment is unsupported by this node.
     *
     *  When this returns `true`, the node is "amendment blocked" — it is
     *  executing ledger rules it does not fully implement. The application
     *  layer should warn operators and eventually halt participation.
     *
     *  @return `true` if at least one enabled amendment is not in this node's
     *      supported list.
     */
    [[nodiscard]] virtual bool
    hasUnsupportedEnabled() const = 0;

    /** Return the projected activation time of the earliest unsupported amendment.
     *
     *  Scans amendments currently holding validator supermajority that are not
     *  supported by this node and returns the time at which the earliest such
     *  amendment is expected to activate (`majorityTime` after it first
     *  achieved supermajority). Updated by `doValidatedLedger`.
     *
     *  @return The projected activation time of the first unsupported amendment
     *      that has achieved majority, or `std::nullopt` if no unsupported
     *      amendment is approaching activation.
     */
    [[nodiscard]] virtual std::optional<NetClock::time_point>
    firstUnsupportedExpected() const = 0;

    /** Serialize all known amendments to JSON for RPC responses.
     *
     *  @param isAdmin `true` to include sensitive or operator-only fields.
     *  @return A `json::Value` object containing the full amendment list with
     *      status, vote, and majority information for each entry.
     */
    [[nodiscard]] virtual json::Value
    getJson(bool isAdmin) const = 0;

    /** Returns a json::ValueType::Object. */
    [[nodiscard]] virtual json::Value
    getJson(uint256 const& amendment, bool isAdmin) const = 0;

    /** Update amendment state from a newly validated ledger.
     *
     *  Adapter that extracts `enabledAmendments` and `majorityAmendments` from
     *  `lastValidatedLedger` via `getEnabledAmendments()` and
     *  `getMajorityAmendments()`, then delegates to the pure-virtual
     *  `doValidatedLedger(LedgerIndex, set, majorityAmendments_t)` overload.
     *  The call is skipped entirely when `needValidatedLedger` returns `false`.
     *
     *  @param lastValidatedLedger The most recently validated ledger. Amendment
     *      state is read from this view; the ledger sequence gates the update.
     */
    void
    doValidatedLedger(std::shared_ptr<ReadView const> const& lastValidatedLedger)
    {
        if (needValidatedLedger(lastValidatedLedger->seq()))
        {
            doValidatedLedger(
                lastValidatedLedger->seq(),
                getEnabledAmendments(*lastValidatedLedger),
                getMajorityAmendments(*lastValidatedLedger));
        }
    }

    /** Return whether the amendment table needs to process a given ledger sequence.
     *
     *  Amendment voting state only changes at flag ledgers (every 256 ledgers).
     *  This gate avoids the cost of extracting and processing amendment state
     *  for the vast majority of validated ledgers that cannot affect voting
     *  outcomes.
     *
     *  @param seq The sequence number of the validated ledger being considered.
     *  @return `true` if `seq` crosses a new 256-ledger flag boundary relative
     *      to the last processed sequence; `false` if no change is possible.
     */
    [[nodiscard]] virtual bool
    needValidatedLedger(LedgerIndex seq) const = 0;

    /** Update internal amendment state from pre-extracted ledger data.
     *
     *  Enables all amendments in `enabled`, then scans `majority` for
     *  unsupported amendments approaching activation and updates the
     *  `firstUnsupportedExpected` projection accordingly. Errors are logged for
     *  each unsupported amendment that has reached supermajority.
     *
     *  @param ledgerSeq Sequence number of the validated ledger.
     *  @param enabled Set of amendment hashes currently active in the ledger.
     *  @param majority Map of amendment hash → time of first observed
     *      supermajority for amendments that have crossed the voting threshold
     *      but are not yet enabled.
     */
    virtual void
    doValidatedLedger(
        LedgerIndex ledgerSeq,
        std::set<uint256> const& enabled,
        majorityAmendments_t const& majority) = 0;

    /** Notify the table that the set of trusted validators has changed.
     *
     *  Updates the internal per-validator vote cache: existing records are
     *  preserved for validators that remain trusted; new validators are
     *  initialized with empty votes; validators no longer in the UNL have
     *  their records discarded. Vote history is NOT reset — this preserves
     *  the anti-flapping behavior that prevents an amendment from appearing to
     *  oscillate across the 80% threshold as validators come and go.
     *
     *  @param allTrusted The complete current set of trusted validator public keys.
     */
    virtual void
    trustChanged(hash_set<PublicKey> const& allTrusted) = 0;

    /** Compute amendment actions for the current consensus round.
     *
     *  Aggregates amendment votes from `valSet` against the current ledger
     *  state, applying the anti-flapping policy that retains the last known
     *  vote from each trusted validator for up to 24 hours. For each amendment
     *  whose vote state has changed relative to the ledger, produces an action
     *  entry:
     *  - `tfGotMajority` — validators have supermajority; ledger does not yet
     *    record it.
     *  - `tfLostMajority` — validators have lost supermajority; ledger still
     *    records it.
     *  - `0` — supermajority has been held for `majorityTime`; enable now.
     *
     *  @param rules     Protocol rules in effect for the ledger being built.
     *  @param closeTime Parent ledger's close time, used to evaluate whether
     *      `majorityTime` has elapsed since first supermajority.
     *  @param enabledAmendments   Set of amendment hashes already active.
     *  @param majorityAmendments  Map of amendment hash → time first achieving
     *      supermajority, for amendments not yet enabled.
     *  @param valSet    Validations from the previous ledger; each carries the
     *      set of amendments the issuing validator supports.
     *  @return A map from amendment hash to action flag for each amendment
     *      requiring a pseudo-transaction in the initial consensus position.
     */
    virtual std::map<uint256, std::uint32_t>
    doVoting(
        Rules const& rules,
        NetClock::time_point closeTime,
        std::set<uint256> const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        std::vector<std::shared_ptr<STValidation>> const& valSet) = 0;

    /** Return the amendment hashes this node wishes to vote for.
     *
     *  Called when building a `STValidation` message. Returns all amendments
     *  that this node supports, has Up-voted, and that are not already active
     *  in the ledger. The result is sorted.
     *
     *  @param enabled The set of amendment hashes currently enabled in the
     *      ledger; enabled amendments are excluded from the returned set.
     *  @return Sorted vector of amendment hashes this node wants to vote for.
     */
    [[nodiscard]] virtual std::vector<uint256>
    doValidation(std::set<uint256> const& enabled) const = 0;

    /** Return all non-vetoed amendments desired for a genesis ledger.
     *
     *  Equivalent to `doValidation({})` — returns every supported, Up-voted
     *  amendment since none are enabled yet. If two amendments must not both be
     *  enabled simultaneously, one must be vetoed before calling this.
     *
     *  @return All known, supported, non-vetoed amendment hashes.
     */
    [[nodiscard]] virtual std::vector<uint256>
    getDesired() const = 0;

    // The function below adapts the API callers expect to the
    // internal amendment table API. This allows the amendment
    // table implementation to be independent of the ledger
    // implementation. These APIs will merge when the view code
    // supports a full ledger API

    /** Run the amendment voting pipeline and inject pseudo-transactions.
     *
     *  Adapter for the consensus engine. Extracts amendment state from
     *  `lastClosedLedger`, delegates to the pure-virtual `doVoting` overload
     *  to determine required actions, then builds a signed-less `STTx` of type
     *  `ttAMENDMENT` for each action and inserts it into `initialPosition`
     *  as a `TnTransactionNm` node. These pseudo-transactions are not user
     *  transactions; they are injected directly into the consensus-agreed
     *  transaction set so validators can process them at flag-ledger close.
     *
     *  @param lastClosedLedger    The most recently closed ledger; supplies
     *      rules, parent close time, enabled amendments, and majority state.
     *  @param parentValidations   Validations received for the parent ledger;
     *      each carries the voting validator's amendment preferences.
     *  @param initialPosition     The SHAMap being built as the node's initial
     *      consensus position; amendment pseudo-transactions are added here.
     *  @param j                   Journal for debug logging of injected
     *      pseudo-transactions.
     */
    void
    doVoting(
        std::shared_ptr<ReadView const> const& lastClosedLedger,
        std::vector<std::shared_ptr<STValidation>> const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition,
        beast::Journal j)
    {
        // Ask implementation what to do
        auto actions = doVoting(
            lastClosedLedger->rules(),
            lastClosedLedger->parentCloseTime(),
            getEnabledAmendments(*lastClosedLedger),
            getMajorityAmendments(*lastClosedLedger),
            parentValidations);

        // Inject appropriate pseudo-transactions
        for (auto const& it : actions)
        {
            STTx const amendTx(ttAMENDMENT, [&it, seq = lastClosedLedger->seq() + 1](auto& obj) {
                obj.setAccountID(sfAccount, AccountID());
                obj.setFieldH256(sfAmendment, it.first);
                obj.setFieldU32(sfLedgerSequence, seq);

                if (it.second != 0)
                    obj.setFieldU32(sfFlags, it.second);
            });

            Serializer s;
            amendTx.add(s);

            JLOG(j.debug()) << "Amendments: Adding pseudo-transaction: "
                            << amendTx.getTransactionID() << ": " << strHex(s.slice()) << ": "
                            << amendTx;

            initialPosition->addGiveItem(
                SHAMapNodeType::TnTransactionNm,
                makeShamapitem(amendTx.getTransactionID(), s.slice()));
        }
    }
};

/** Create the concrete AmendmentTable implementation.
 *
 *  Registers all supported amendments, applies config-forced enables and
 *  vetoes, and loads any persisted vote overrides from the wallet database.
 *  Config entries in `enabled` and `vetoed` are ignored if the wallet database
 *  already contains a `FeatureVotes` table — the database is the authoritative
 *  source for persisted vote state.
 *
 *  @param registry      Service registry used to access the wallet database for
 *      persisting vote state.
 *  @param majorityTime  Duration a supermajority must be continuously held
 *      before an amendment is enabled (typically two weeks on mainnet).
 *  @param supported     All amendments compiled into this build, each with its
 *      `VoteBehavior`. Amendments absent from this list are treated as
 *      unsupported; enabling them sets `hasUnsupportedEnabled()`.
 *  @param enabled       Config section (`[amendments]`) listing amendment IDs
 *      to force-enable; applied only when the wallet database has no
 *      `FeatureVotes` table.
 *  @param vetoed        Config section (`[veto_amendments]`) listing amendment
 *      IDs to suppress votes for; applied only when the wallet database has no
 *      `FeatureVotes` table.
 *  @param journal       Journal for logging during initialization.
 *  @return Owning pointer to the constructed `AmendmentTable`.
 */
std::unique_ptr<AmendmentTable>
makeAmendmentTable(
    ServiceRegistry& registry,
    std::chrono::seconds majorityTime,
    std::vector<AmendmentTable::FeatureInfo> const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal);

}  // namespace xrpl
