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
static LimitRange constexpr kAccountLines = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_channels command. */
static LimitRange constexpr kAccountChannels = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_objects command. */
static LimitRange constexpr kAccountObjects = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_offers command. */
static LimitRange constexpr kAccountOffers = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_tx command. */
static LimitRange constexpr kAccountTx = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the book_offers command. */
static LimitRange constexpr kBookOffers = {.rmin = 1, .rDefault = 60, .rmax = 100};

/** Limits for the no_ripple_check command. */
static LimitRange constexpr kNoRippleCheck = {.rmin = 10, .rDefault = 300, .rmax = 400};

/** Limits for the account_nftokens command, in pages. */
static LimitRange constexpr kAccountNfTokens = {.rmin = 20, .rDefault = 100, .rmax = 400};

/** Limits for the nft_buy_offers & nft_sell_offers commands. */
static LimitRange constexpr kNftOffers = {.rmin = 50, .rDefault = 250, .rmax = 500};

static int constexpr kDefaultAutoFillFeeMultiplier = 10;
static int constexpr kDefaultAutoFillFeeDivisor = 1;
static int constexpr kMaxPathfindsInProgress = 2;
static int constexpr kMaxPathfindJobCount = 50;
static int constexpr kMaxJobQueueClients = 500;
auto constexpr kMaxValidatedLedgerAge = std::chrono::minutes{2};
static int constexpr kMaxRequestSize = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static int constexpr kBinaryPageLength = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static int constexpr kJsonPageLength = 256;

/** Maximum number of pages in a LedgerData response. */
int constexpr pageLength(bool isBinary)
{
    return isBinary ? kBinaryPageLength : kJsonPageLength;
}

/** Maximum number of source currencies allowed in a path find request. */
static int constexpr kMaxSrcCur = 18;

/** Maximum number of auto source currencies in a path find request. */
static int constexpr kMaxAutoSrcCur = 88;

}  // namespace xrpl::RPC::Tuning
/** @} */
