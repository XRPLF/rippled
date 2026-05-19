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
static LimitRange constexpr accountLines = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_channels command. */
static LimitRange constexpr accountChannels = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_objects command. */
static LimitRange constexpr accountObjects = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_offers command. */
static LimitRange constexpr accountOffers = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_tx command. */
static LimitRange constexpr accountTx = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the book_offers command. */
static LimitRange constexpr bookOffers = {.rmin = 1, .rDefault = 60, .rmax = 100};

/** Limits for the no_ripple_check command. */
static LimitRange constexpr noRippleCheck = {.rmin = 10, .rDefault = 300, .rmax = 400};

/** Limits for the account_nftokens command, in pages. */
static LimitRange constexpr accountNFTokens = {.rmin = 20, .rDefault = 100, .rmax = 400};

/** Limits for the nft_buy_offers & nft_sell_offers commands. */
static LimitRange constexpr nftOffers = {.rmin = 50, .rDefault = 250, .rmax = 500};

static int constexpr defaultAutoFillFeeMultiplier = 10;
static int constexpr defaultAutoFillFeeDivisor = 1;
static int constexpr maxPathfindsInProgress = 2;
static int constexpr maxPathfindJobCount = 50;
static int constexpr maxJobQueueClients = 500;
auto constexpr maxValidatedLedgerAge = std::chrono::minutes{2};
static int constexpr maxRequestSize = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static int constexpr binaryPageLength = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static int constexpr jsonPageLength = 256;

/** Maximum number of pages in a LedgerData response. */
int constexpr pageLength(bool isBinary)
{
    return isBinary ? binaryPageLength : jsonPageLength;
}

/** Maximum number of source currencies allowed in a path find request. */
static int constexpr max_src_cur = 18;

/** Maximum number of auto source currencies in a path find request. */
static int constexpr max_auto_src_cur = 88;

}  // namespace xrpl::RPC::Tuning
/** @} */
