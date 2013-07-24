//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ChangeTransactor : public Transactor
{
public:
    ChangeTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine)
        : Transactor (txn, params, engine)
    {
        ;
    }

    TER doApply ();
    TER checkSig ();
    TER checkSeq ();
    TER payFee ();
    TER preCheck ();

private:
    TER applyFeature ();
    TER applyFee ();

    // VFALCO TODO Can this be removed?
    bool mustHaveValidAccount ()
    {
        return false;
    }
};

// vim:ts=4
