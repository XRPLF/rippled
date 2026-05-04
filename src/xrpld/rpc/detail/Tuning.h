#pragma once

/** Tuned constants. */
/** @{ */
namespace xrpl::RPC::Tuning {

/** Represents RPC limit parameter values that have a min, default and max. */
struct LimitRange
{
    unsigned int rmin, rDefault, rmax;
};

/** Limits for the account_lines command. */
static LimitRange constexpr kACCOUNT_LINES = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_channels command. */
static LimitRange constexpr kACCOUNT_CHANNELS = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_objects command. */
static LimitRange constexpr kACCOUNT_OBJECTS = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_offers command. */
static LimitRange constexpr kACCOUNT_OFFERS = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_tx command. */
static LimitRange constexpr kACCOUNT_TX = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the book_offers command. */
static LimitRange constexpr kBOOK_OFFERS = {.rmin = 1, .rDefault = 60, .rmax = 100};

/** Limits for the no_ripple_check command. */
static LimitRange constexpr kNO_RIPPLE_CHECK = {.rmin = 10, .rDefault = 300, .rmax = 400};

/** Limits for the account_nftokens command, in pages. */
static LimitRange constexpr kACCOUNT_NF_TOKENS = {.rmin = 20, .rDefault = 100, .rmax = 400};

/** Limits for the nft_buy_offers & nft_sell_offers commands. */
static LimitRange constexpr kNFT_OFFERS = {.rmin = 50, .rDefault = 250, .rmax = 500};

static int constexpr kDEFAULT_AUTO_FILL_FEE_MULTIPLIER = 10;
static int constexpr kDEFAULT_AUTO_FILL_FEE_DIVISOR = 1;
static int constexpr kMAX_PATHFINDS_IN_PROGRESS = 2;
static int constexpr kMAX_PATHFIND_JOB_COUNT = 50;
static int constexpr kMAX_JOB_QUEUE_CLIENTS = 500;
auto constexpr kMAX_VALIDATED_LEDGER_AGE = std::chrono::minutes{2};
static int constexpr kMAX_REQUEST_SIZE = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static int constexpr kBINARY_PAGE_LENGTH = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static int constexpr kJSON_PAGE_LENGTH = 256;

/** Maximum number of pages in a LedgerData response. */
int constexpr pageLength(bool isBinary)
{
    return isBinary ? kBINARY_PAGE_LENGTH : kJSON_PAGE_LENGTH;
}

/** Maximum number of source currencies allowed in a path find request. */
static int constexpr kMAX_SRC_CUR = 18;

/** Maximum number of auto source currencies in a path find request. */
static int constexpr kMAX_AUTO_SRC_CUR = 88;

}  // namespace xrpl::RPC::Tuning
/** @} */
