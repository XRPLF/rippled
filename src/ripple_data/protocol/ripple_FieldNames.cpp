//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// These must stay at the top of this file
std::map<int, SField::ptr> SField::codeToField;
int SField::num = 0;


// Solve construction issues for objects with static storage duration.
SField::StaticLockType& SField::getMutex ()
{
    static StaticLockType mutex ("SField", __FILE__, __LINE__);
    return mutex;
}

SField sfInvalid (-1), sfGeneric (0);
SField sfLedgerEntry (STI_LEDGERENTRY, 1, "LedgerEntry");
SField sfTransaction (STI_TRANSACTION, 1, "Transaction");
SField sfValidation (STI_VALIDATION, 1, "Validation");
SField sfHash (STI_HASH256, 257, "hash");
SField sfIndex (STI_HASH256, 258, "index");

#define FIELD(name, type, index) SField sf##name(FIELD_CODE(STI_##type, index), STI_##type, index, #name);
#define TYPE(name, type, index)
#include "../protocol/ripple_SerializeDeclarations.h"
#undef FIELD
#undef TYPE

static int initFields ()
{
    sfTxnSignature.notSigningField ();
    sfTxnSignatures.notSigningField ();
    sfSignature.notSigningField ();

    sfIndexes.setMeta (SField::sMD_Never);
    sfPreviousTxnID.setMeta (SField::sMD_DeleteFinal);
    sfPreviousTxnLgrSeq.setMeta (SField::sMD_DeleteFinal);
    sfLedgerEntryType.setMeta (SField::sMD_Never);
    sfRootIndex.setMeta (SField::sMD_Always);

    return 0;
}
static const int f = initFields ();


SField::SField (SerializedTypeID tid, int fv) : fieldCode (FIELD_CODE (tid, fv)), fieldType (tid), fieldValue (fv),
    fieldMeta (sMD_Default), fieldNum (++num), signingField (true)
{
    // call with the map mutex
    fieldName = lexicalCast <std::string> (tid) + "/" + lexicalCast <std::string> (fv);
    codeToField[fieldCode] = this;
    assert ((fv != 1) || ((tid != STI_ARRAY) && (tid != STI_OBJECT)));
}

SField::ref SField::getField (int code)
{
    int type = code >> 16;
    int field = code % 0xffff;

    if ((type <= 0) || (field <= 0))
        return sfInvalid;

    StaticScopedLockType sl (getMutex (), __FILE__, __LINE__);

    std::map<int, SField::ptr>::iterator it = codeToField.find (code);

    if (it != codeToField.end ())
        return * (it->second);

    if (field > 255)        // don't dynamically extend types that have no binary encoding
        return sfInvalid;

    switch (type)
    {
        // types we are willing to dynamically extend

#define FIELD(name, type, index)
#define TYPE(name, type, index) case STI_##type:
#include "../protocol/ripple_SerializeDeclarations.h"
#undef FIELD
#undef TYPE

        break;

    default:
        return sfInvalid;
    }

    return * (new SField (static_cast<SerializedTypeID> (type), field));
}

int SField::compare (SField::ref f1, SField::ref f2)
{
    // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
    if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
        return 0;

    if (f1.fieldCode < f2.fieldCode)
        return -1;

    if (f2.fieldCode < f1.fieldCode)
        return 1;

    return 0;
}

std::string SField::getName () const
{
    if (!fieldName.empty ())
        return fieldName;

    if (fieldValue == 0)
        return "";

    return lexicalCastThrow <std::string> (static_cast<int> (fieldType)) + "/" +
           lexicalCastThrow <std::string> (fieldValue);
}

SField::ref SField::getField (const std::string& fieldName)
{
    // OPTIMIZEME me with a map. CHECKME this is case sensitive
    StaticScopedLockType sl (getMutex (), __FILE__, __LINE__);
    typedef std::map<int, SField::ptr>::value_type int_sfref_pair;
    BOOST_FOREACH (const int_sfref_pair & fieldPair, codeToField)
    {
        if (fieldPair.second->fieldName == fieldName)
            return * (fieldPair.second);
    }
    return sfInvalid;
}

SField::~SField ()
{
    StaticScopedLockType sl (getMutex (), __FILE__, __LINE__);
    std::map<int, ptr>::iterator it = codeToField.find (fieldCode);

    if ((it != codeToField.end ()) && (it->second == this))
        codeToField.erase (it);
}

// vim:ts=4
