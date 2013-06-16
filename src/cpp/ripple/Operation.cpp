//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*
We also need to charge for each op

*/

namespace Script
{


int Operation::getFee ()
{
    return (theConfig.FEE_CONTRACT_OPERATION);
}

}