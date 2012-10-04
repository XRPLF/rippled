#include "Operation.h"
#include "Config.h"

/*
We also need to charge for each op

*/

namespace Script {


int Operation::getFee()
{
	return(theConfig.FEE_CONTRACT_OPERATION);
}

}