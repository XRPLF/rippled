#ifndef XRPL_APP_LEDGER_LEDGERCLEANER_H_INCLUDED
#define XRPL_APP_LEDGER_LEDGERCLEANER_H_INCLUDED

#include <xrpld/app/main/Application.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/json_value.h>

namespace ripple {

/** Check the ledger/transaction databases to make sure they have continuity */
class LedgerCleaner : public beast::PropertyStream::Source
{
protected:
    LedgerCleaner() : beast::PropertyStream::Source("ledgercleaner")
    {
    }

public:
    virtual ~LedgerCleaner() = default;

    virtual void
    start() = 0;

    virtual void
    stop() = 0;

    /** Start a long running task to clean the ledger.
        The ledger is cleaned asynchronously, on an implementation defined
        thread. This function call does not block. The long running task
        will be stopped by a call to stop().

        Thread safety:
            Safe to call from any thread at any time.

        @param parameters A Json object with configurable parameters.
    */
    virtual void
    clean(Json::Value const& parameters) = 0;
};

std::unique_ptr<LedgerCleaner>
make_LedgerCleaner(Application& app, beast::Journal journal);

}  // namespace ripple

#endif
