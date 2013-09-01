//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_RIPPLEHEADER
#define RIPPLE_CORE_RIPPLEHEADER

#include "../ripple_basics/ripple_basics.h"
#include "../ripple_data/ripple_data.h"

namespace ripple
{

// Order matters

# include "functional/ripple_ConfigSections.h"
#include "functional/ripple_Config.h"
#include "functional/ripple_ILoadFeeTrack.h"
#  include "functional/ripple_LoadEvent.h"
#  include "functional/ripple_LoadMonitor.h"
# include "functional/ripple_Job.h"
#include "functional/ripple_JobQueue.h"
# include "functional/LoadType.h"
#include "functional/LoadSource.h"

#include "node/NodeObject.h"
#include "node/NodeStore.h"

#include "validator/ripple_Validator.h"
#include "validator/ripple_ValidatorList.h"
#include "validator/ripple_Validators.h"
#include "validator/ripple_StringsValidatorSource.h"
#include "validator/ripple_TrustedUriValidatorSource.h"

}

#endif
