#ifndef XRPL_APP_LEDGER_ACCEPTEDLEDGER_H_INCLUDED
#define XRPL_APP_LEDGER_ACCEPTEDLEDGER_H_INCLUDED

#include <xrpld/app/ledger/AcceptedLedgerTx.h>

namespace ripple {

/** A ledger that has become irrevocable.

    An accepted ledger is a ledger that has a sufficient number of
    validations to convince the local server that it is irrevocable.

    The existence of an accepted ledger implies all preceding ledgers
    are accepted.
*/
/* VFALCO TODO digest this terminology clarification:
    Closed and accepted refer to ledgers that have not passed the
    validation threshold yet. Once they pass the threshold, they are
    "Validated". Closed just means its close time has passed and no
    new transactions can get in. "Accepted" means we believe it to be
    the result of the a consensus process (though haven't validated
    it yet).
*/
class AcceptedLedger : public CountedObject<AcceptedLedger>
{
public:
    AcceptedLedger(
        std::shared_ptr<ReadView const> const& ledger,
        Application& app);

    std::shared_ptr<ReadView const> const&
    getLedger() const
    {
        return mLedger;
    }

    std::size_t
    size() const
    {
        return transactions_.size();
    }

    auto
    begin() const
    {
        return transactions_.begin();
    }

    auto
    end() const
    {
        return transactions_.end();
    }

private:
    std::shared_ptr<ReadView const> mLedger;
    std::vector<std::unique_ptr<AcceptedLedgerTx>> transactions_;
};

}  // namespace ripple

#endif
