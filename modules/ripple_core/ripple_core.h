//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Include this to get the @ref ripple_core module.

    @file ripple_core.h
    @ingroup ripple_core
*/

/** Core classes.

    These objects form the execution framework in which the Ripple
    protocol is implemented.

    @defgroup ripple_core
*/

#ifndef RIPPLE_CORE_RIPPLEHEADER
#define RIPPLE_CORE_RIPPLEHEADER

#include "../ripple_basics/ripple_basics.h"

#include "../ripple_data/ripple_data.h"

namespace ripple
{

// VFALCO NOTE Indentation shows dependency hierarchy
//
/**/#include "functional/ripple_Config.h"
/**/#include "functional/ripple_ILoadFeeTrack.h"
/*..*/#include "functional/ripple_LoadEvent.h"
/*..*/#include "functional/ripple_LoadMonitor.h"
/*.*/#include "functional/ripple_Job.h"
/**/#include "functional/ripple_JobQueue.h"

#include "validator/ripple_Validator.h"
#include "validator/ripple_ValidatorList.h"
#include "validator/ripple_ChosenValidators.h"
#include "validator/ripple_Validators.h"
#include "validator/ripple_StringsValidatorSource.h"
#include "validator/ripple_TrustedUriValidatorSource.h"

}

#endif
