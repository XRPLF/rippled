#ifndef CONTRACT_H
#define CONTRACT_H

/*
	Encapsulates the SLE for a Contract
*/

class Contract
{
public:
	Contract();

	uint160& getIssuer();
	uint160& getOwner();
	STAmount& getRippleEscrow();
	uint32 getEscrow();
	uint32 getBond();

	Script::Data getData(int index);

	void executeCreate();
	void executeRemove();
	void executeFund();
	void executeAccept();
};

#endif

// vim:ts=4
