#include <string>
#include "Application.h"
#include "ScopedLock.h"
#include "Serializer.h"
#include "Wallet.h"
#include "Ledger.h"
#include "SHAMap.h"

int main()
{
	theApp = new Application();
	theApp->run();

    Serializer::TestSerializer();
    SHAMapNode::ClassInit();
    SHAMap::TestSHAMap();
    Ledger::unitTest();
    return 0;
}
