#include <string>
#include "Application.h"
#include "ScopedLock.h"
#include "Serializer.h"
#include "Wallet.h"
#include "Ledger.h"
#include "SHAMap.h"
#include "DeterministicKeys.h"

int main()
{
	theApp = new Application();
	theApp->run();

    DetKeySet::unitTest();
    Serializer::TestSerializer();
    SHAMapNode::ClassInit();
    SHAMap::TestSHAMap();
    Ledger::unitTest();
    return 0;
}
