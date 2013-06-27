//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

BOOST_AUTO_TEST_SUITE (SerializedObject)

BOOST_AUTO_TEST_CASE ( FieldManipulation_test )
{
    if (sfGeneric.isUseful ())
        BOOST_FAIL ("sfGeneric must not be useful");

    SField sfTestVL (STI_VL, 255, "TestVL");
    SField sfTestH256 (STI_HASH256, 255, "TestH256");
    SField sfTestU32 (STI_UINT32, 255, "TestU32");
    SField sfTestObject (STI_OBJECT, 255, "TestObject");

    SOTemplate elements;
    elements.push_back (SOElement (sfFlags, SOE_REQUIRED));
    elements.push_back (SOElement (sfTestVL, SOE_REQUIRED));
    elements.push_back (SOElement (sfTestH256, SOE_OPTIONAL));
    elements.push_back (SOElement (sfTestU32, SOE_REQUIRED));

    STObject object1 (elements, sfTestObject);
    STObject object2 (object1);

    if (object1.getSerializer () != object2.getSerializer ()) BOOST_FAIL ("STObject error 1");

    if (object1.isFieldPresent (sfTestH256) || !object1.isFieldPresent (sfTestVL))
        BOOST_FAIL ("STObject error");

    object1.makeFieldPresent (sfTestH256);

    if (!object1.isFieldPresent (sfTestH256)) BOOST_FAIL ("STObject Error 2");

    if (object1.getFieldH256 (sfTestH256) != uint256 ()) BOOST_FAIL ("STObject error 3");

    if (object1.getSerializer () == object2.getSerializer ())
    {
        WriteLog (lsINFO, STObject) << "O1: " << object1.getJson (0);
        WriteLog (lsINFO, STObject) << "O2: " << object2.getJson (0);
        BOOST_FAIL ("STObject error 4");
    }

    object1.makeFieldAbsent (sfTestH256);

    if (object1.isFieldPresent (sfTestH256)) BOOST_FAIL ("STObject error 5");

    if (object1.getFlags () != 0) BOOST_FAIL ("STObject error 6");

    if (object1.getSerializer () != object2.getSerializer ()) BOOST_FAIL ("STObject error 7");

    STObject copy (object1);

    if (object1.isFieldPresent (sfTestH256)) BOOST_FAIL ("STObject error 8");

    if (copy.isFieldPresent (sfTestH256)) BOOST_FAIL ("STObject error 9");

    if (object1.getSerializer () != copy.getSerializer ()) BOOST_FAIL ("STObject error 10");

    copy.setFieldU32 (sfTestU32, 1);

    if (object1.getSerializer () == copy.getSerializer ()) BOOST_FAIL ("STObject error 11");

    for (int i = 0; i < 1000; i++)
    {
        Blob j (i, 2);

        object1.setFieldVL (sfTestVL, j);

        Serializer s;
        object1.add (s);
        SerializerIterator it (s);

        STObject object3 (elements, it, sfTestObject);

        if (object1.getFieldVL (sfTestVL) != j) BOOST_FAIL ("STObject error");

        if (object3.getFieldVL (sfTestVL) != j) BOOST_FAIL ("STObject error");
    }
}

BOOST_AUTO_TEST_SUITE_END ();
