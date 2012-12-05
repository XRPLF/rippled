#ifndef LOADTYPES__H
#define LOADTYPES__H

enum LoadType
{ // types of load that can be placed on the server

	// Bad things
	LT_InvalidRequest,			// A request that we can immediately tell is invalid
	LT_RequestNoReply,			// A request that we cannot satisfy
	LT_InvalidSignature,		// An object whose signature we had to check and it failed
	LT_UnwantedData,			// Data we have no use for

	// Good things
	LT_NewTrusted,				// A new transaction/validation/proposal we trust
	LT_NewTransaction,			// A new, valid transaction
	LT_NeededData,				// Data we requested

	// Requests
	LT_RequestData,				// A request that is hard to satisfy, disk access
	LT_CheapQuery,				// A query that is trivial, cached data
};

static const int LoadCategoryDisk		= 1;
static const int LoadCategoryCPU		= 2;
static const int LoadCateogryNetwork	= 4;

#endif
