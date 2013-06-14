#ifndef RIPPLE_ACCOUNTITEMS_H
#define RIPPLE_ACCOUNTITEMS_H

/** A set of AccountItem objects.
*/
class AccountItems
{
public:
    typedef boost::shared_ptr <AccountItems> pointer;

    typedef std::vector <AccountItem::pointer> Container;

    // VFALCO TODO Create a typedef uint160 AccountID and replace
    AccountItems (uint160 const& accountID,
                  Ledger::ref ledger,
                  AccountItem::pointer ofType);

    // VFALCO TODO rename to getContainer and make this change in every interface
    //             that exposes the caller to the type of container.
    //
    Container& getItems ()
    {
        return mItems;
    }

    // VFALCO TODO What is the int for?
    Json::Value getJson (int);

private:
    void fillItems (const uint160& accountID, Ledger::ref ledger);

private:
    // VFALCO TODO This looks like its used as an exemplar, rename appropriately
    AccountItem::pointer mOfType;

    Container mItems;
};

#endif
