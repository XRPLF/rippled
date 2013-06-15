#ifndef RIPPLE_TXFORMATS_H
#define RIPPLE_TXFORMATS_H

/** Manages the list of known transaction formats.
*/
class TxFormats
{
public:
    // VFALCO TODO Make this a member of the Application object instead of a singleton?
    static TxFormats& getInstance ();

    /** Add a format.

        The caller is responsible for freeing the memory.

        @return The passed format.
    */
    TxFormat* add (TxFormat* txFormat);

    /** Retrieve a format based on its transaction type.
    */
    TxFormat* findByType (TransactionType type);

    /** Retrieve a format based on its name.
    */
    TxFormat* findByName (std::string const& name);

private:
    TxFormats ();

private:
    typedef std::map <std::string, TxFormat*> NameMap;
    typedef std::map <TransactionType, TxFormat*> TypeMap;

    NameMap m_names;
    TypeMap m_types;
};

#endif
// vim:ts=4
