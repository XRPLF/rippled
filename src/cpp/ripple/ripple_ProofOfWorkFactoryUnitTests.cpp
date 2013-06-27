//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

BOOST_AUTO_TEST_SUITE (ProofOfWork_suite)

BOOST_AUTO_TEST_CASE ( ProofOfWork_test )
{
    using namespace ripple;

    ProofOfWorkFactory gen;
    ProofOfWork pow = gen.getProof ();
    WriteLog (lsINFO, ProofOfWork) << "Estimated difficulty: " << pow.getDifficulty ();
    uint256 solution = pow.solve (16777216);

    if (solution.isZero ())
        BOOST_FAIL ("Unable to solve proof of work");

    if (!pow.checkSolution (solution))
        BOOST_FAIL ("Solution did not check");

    WriteLog (lsDEBUG, ProofOfWork) << "A bad nonce error is expected";
    POWResult r = gen.checkProof (pow.getToken (), uint256 ());

    if (r != powBADNONCE)
    {
        Log (lsFATAL) << "POWResult = " << static_cast<int> (r);
        BOOST_FAIL ("Empty solution didn't show bad nonce");
    }

    if (gen.checkProof (pow.getToken (), solution) != powOK)
        BOOST_FAIL ("Solution did not check with issuer");

    WriteLog (lsDEBUG, ProofOfWork) << "A reused nonce error is expected";

    if (gen.checkProof (pow.getToken (), solution) != powREUSED)
        BOOST_FAIL ("Reuse solution not detected");

#ifdef SOLVE_POWS

    for (int i = 0; i < 12; ++i)
    {
        gen.setDifficulty (i);
        ProofOfWork pow = gen.getProof ();
        WriteLog (lsINFO, ProofOfWork) << "Level: " << i << ", Estimated difficulty: " << pow.getDifficulty ();
        uint256 solution = pow.solve (131072);

        if (solution.isZero ())
            WriteLog (lsINFO, ProofOfWork) << "Giving up";
        else
        {
            WriteLog (lsINFO, ProofOfWork) << "Solution found";

            if (gen.checkProof (pow.getToken (), solution) != powOK)
            {
                WriteLog (lsFATAL, ProofOfWork) << "Solution fails";
            }
        }
    }

#endif

}

BOOST_AUTO_TEST_SUITE_END ()
