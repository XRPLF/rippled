//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================
BOOST_AUTO_TEST_SUITE (SerializedTransactionTS)

BOOST_AUTO_TEST_CASE ( STrans_test )
{
    RippleAddress seed;
    seed.setSeedRandom ();
    RippleAddress generator = RippleAddress::createGeneratorPublic (seed);
    RippleAddress publicAcct = RippleAddress::createAccountPublic (generator, 1);
    RippleAddress privateAcct = RippleAddress::createAccountPrivate (generator, seed, 1);

    SerializedTransaction j (ttACCOUNT_SET);
    j.setSourceAccount (publicAcct);
    j.setSigningPubKey (publicAcct);
    j.setFieldVL (sfMessageKey, publicAcct.getAccountPublic ());
    j.sign (privateAcct);

    if (!j.checkSign ()) BOOST_FAIL ("Transaction fails signature test");

    Serializer rawTxn;
    j.add (rawTxn);
    SerializerIterator sit (rawTxn);
    SerializedTransaction copy (sit);

    if (copy != j)
    {
        Log (lsFATAL) << j.getJson (0);
        Log (lsFATAL) << copy.getJson (0);
        BOOST_FAIL ("Transaction fails serialize/deserialize test");
    }

    UPTR_T<STObject> new_obj = STObject::parseJson (j.getJson (0), sfGeneric);

    if (new_obj.get () == NULL) BOOST_FAIL ("Unable to build object from json");

    if (STObject (j) != *new_obj)
    {
        Log (lsINFO) << "ORIG: " << j.getJson (0);
        Log (lsINFO) << "BUILT " << new_obj->getJson (0);
        BOOST_FAIL ("Built a different transaction");
    }
}

BOOST_AUTO_TEST_SUITE_END ();
