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
static constexpr LimitRange kAccountLines = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_channels command. */
static constexpr LimitRange kAccountChannels = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_objects command. */
static constexpr LimitRange kAccountObjects = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_offers command. */
static constexpr LimitRange kAccountOffers = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the account_tx command. */
static constexpr LimitRange kAccountTx = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Limits for the book_offers command. */
static constexpr LimitRange kBookOffers = {.rmin = 1, .rDefault = 60, .rmax = 100};

/** Limits for the no_ripple_check command. */
static constexpr LimitRange kNoRippleCheck = {.rmin = 10, .rDefault = 300, .rmax = 400};

/** Limits for the account_nftokens command, in pages. */
static constexpr LimitRange kAccountNfTokens = {.rmin = 20, .rDefault = 100, .rmax = 400};

/** Limits for the nft_buy_offers & nft_sell_offers commands. */
static constexpr LimitRange kNftOffers = {.rmin = 50, .rDefault = 250, .rmax = 500};

static constexpr int kDefaultAutoFillFeeMultiplier = 10;
static constexpr int kDefaultAutoFillFeeDivisor = 1;
static constexpr int kMaxPathfindsInProgress = 2;
static constexpr int kMaxPathfindJobCount = 50;
static constexpr int kMaxJobQueueClients = 500;
constexpr auto kMaxValidatedLedgerAge = std::chrono::minutes{2};
static constexpr int kMaxRequestSize = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static constexpr int kBinaryPageLength = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static constexpr int kJsonPageLength = 256;

/** Maximum number of pages in a LedgerData response. */
constexpr int
pageLength(bool isBinary)
{
    return isBinary ? kBinaryPageLength : kJsonPageLength;
}

/** Maximum number of source currencies allowed in a path find request. */
static constexpr int kMaxSrcCur = 18;

/** Maximum number of auto source currencies in a path find request. */
static constexpr int kMaxAutoSrcCur = 88;

}  // namespace xrpl::RPC::Tuning
/** @} */
