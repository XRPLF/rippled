//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TXFORMATS_H_INCLUDED
#define RIPPLE_TXFORMATS_H_INCLUDED

/** Transaction type identifiers.

    These are part of the binary message format.

    @ingroup protocol
*/
enum TxType
{
    ttINVALID           = -1,

    ttPAYMENT           = 0,
    ttCLAIM             = 1, // open
    ttWALLET_ADD        = 2,
    ttACCOUNT_SET       = 3,
    ttPASSWORD_FUND     = 4, // open
    ttREGULAR_KEY_SET   = 5,
    ttNICKNAME_SET      = 6, // open
    ttOFFER_CREATE      = 7,
    ttOFFER_CANCEL      = 8,
    ttCONTRACT          = 9,
    ttCONTRACT_REMOVE   = 10,  // can we use the same msg as offer cancel

    ttTRUST_SET         = 20,

    ttFEATURE           = 100,
    ttFEE               = 101,
};

/** Manages the list of known transaction formats.
*/
class TxFormats
{
public:
    /** A transaction format.
    */
    class Item
    {
    public:
        Item (char const* name, TxType type);

        Item& operator<< (SOElement const& el);

        /** Retrieve the name of the format.
        */
        std::string const& getName () const noexcept;

        /** Retrieve the transaction type this format represents.
        */
        TxType getType () const noexcept;

    public:
        // VFALCO TODO make an accessor for this
        SOTemplate elements;

    private:
        std::string const m_name;
        TxType const m_type;
    };

private:
    /** Create the object.

        This will load the object will all the known transaction formats.
    */
    TxFormats ();

    static void addCommonFields (Item& item);

public:
    /** Retrieve the global instance.
    */
    static TxFormats const& getInstance ();

    /** Retrieve the type for a transaction format specified by name.

        If the format name is unknown, an exception is thrown.

        @param  name The name of the transaction type.
        @return      The transaction type.
    */
    TxType findTypeByName (std::string const name) const;

    /** Retrieve a format based on its transaction type.
    */
    Item const* findByType (TxType type) const noexcept;

    /** Retrieve a format based on its name.
    */
    Item const* findByName (std::string const& name) const noexcept;

protected:
    /** Add a new format.

        The new format has the set of common fields already added.

        @param name The name of this format.
        @param type The transaction type of this format.

        @return The created format.
    */
    Item& add (char const* name, TxType type);

private:
    // VFALCO TODO use String instead of std::string
    typedef std::map <std::string, Item*> NameMap;
    typedef std::map <TxType, Item*> TypeMap;

    OwnedArray <Item> m_formats;
    NameMap m_names;
    TypeMap m_types;
};

#endif
