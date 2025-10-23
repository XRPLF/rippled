#ifndef XRPL_APP_MISC_AMENDMENTTABLE_H_INCLUDED
#define XRPL_APP_MISC_AMENDMENTTABLE_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STValidation.h>

#include <optional>

namespace ripple {

/** The amendment table stores the list of enabled and potential amendments.
    Individuals amendments are voted on by validators during the consensus
    process.
*/
class AmendmentTable
{
public:
    struct FeatureInfo
    {
        FeatureInfo() = delete;
        FeatureInfo(std::string const& n, uint256 const& f, VoteBehavior v)
            : name(n), feature(f), vote(v)
        {
        }

        std::string const name;
        uint256 const feature;
        VoteBehavior const vote;
    };

    virtual ~AmendmentTable() = default;

    virtual uint256
    find(std::string const& name) const = 0;

    virtual bool
    veto(uint256 const& amendment) = 0;
    virtual bool
    unVeto(uint256 const& amendment) = 0;

    virtual bool
    enable(uint256 const& amendment) = 0;

    virtual bool
    isEnabled(uint256 const& amendment) const = 0;
    virtual bool
    isSupported(uint256 const& amendment) const = 0;

    /**
     * @brief returns true if one or more amendments on the network
     * have been enabled that this server does not support
     *
     * @return true if an unsupported feature is enabled on the network
     */
    virtual bool
    hasUnsupportedEnabled() const = 0;

    virtual std::optional<NetClock::time_point>
    firstUnsupportedExpected() const = 0;

    virtual Json::Value
    getJson(bool isAdmin) const = 0;

    /** Returns a Json::objectValue. */
    virtual Json::Value
    getJson(uint256 const& amendment, bool isAdmin) const = 0;

    /** Called when a new fully-validated ledger is accepted. */
    void
    doValidatedLedger(
        std::shared_ptr<ReadView const> const& lastValidatedLedger)
    {
        if (needValidatedLedger(lastValidatedLedger->seq()))
            doValidatedLedger(
                lastValidatedLedger->seq(),
                getEnabledAmendments(*lastValidatedLedger),
                getMajorityAmendments(*lastValidatedLedger));
    }

    /** Called to determine whether the amendment logic needs to process
        a new validated ledger. (If it could have changed things.)
    */
    virtual bool
    needValidatedLedger(LedgerIndex seq) const = 0;

    virtual void
    doValidatedLedger(
        LedgerIndex ledgerSeq,
        std::set<uint256> const& enabled,
        majorityAmendments_t const& majority) = 0;

    // Called when the set of trusted validators changes.
    virtual void
    trustChanged(hash_set<PublicKey> const& allTrusted) = 0;

    // Called by the consensus code when we need to
    // inject pseudo-transactions
    virtual std::map<uint256, std::uint32_t>
    doVoting(
        Rules const& rules,
        NetClock::time_point closeTime,
        std::set<uint256> const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        std::vector<std::shared_ptr<STValidation>> const& valSet) = 0;

    // Called by the consensus code when we need to
    // add feature entries to a validation
    virtual std::vector<uint256>
    doValidation(std::set<uint256> const& enabled) const = 0;

    // The set of amendments to enable in the genesis ledger
    // This will return all known, non-vetoed amendments.
    // If we ever have two amendments that should not both be
    // enabled at the same time, we should ensure one is vetoed.
    virtual std::vector<uint256>
    getDesired() const = 0;

    // The function below adapts the API callers expect to the
    // internal amendment table API. This allows the amendment
    // table implementation to be independent of the ledger
    // implementation. These APIs will merge when the view code
    // supports a full ledger API

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
            STTx amendTx(
                ttAMENDMENT,
                [&it, seq = lastClosedLedger->seq() + 1](auto& obj) {
                    obj.setAccountID(sfAccount, AccountID());
                    obj.setFieldH256(sfAmendment, it.first);
                    obj.setFieldU32(sfLedgerSequence, seq);

                    if (it.second != 0)
                        obj.setFieldU32(sfFlags, it.second);
                });

            Serializer s;
            amendTx.add(s);

            JLOG(j.debug()) << "Amendments: Adding pseudo-transaction: "
                            << amendTx.getTransactionID() << ": "
                            << strHex(s.slice()) << ": " << amendTx;

            initialPosition->addGiveItem(
                SHAMapNodeType::tnTRANSACTION_NM,
                make_shamapitem(amendTx.getTransactionID(), s.slice()));
        }
    }
};

std::unique_ptr<AmendmentTable>
make_AmendmentTable(
    Application& app,
    std::chrono::seconds majorityTime,
    std::vector<AmendmentTable::FeatureInfo> const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal);

}  // namespace ripple

#endif
