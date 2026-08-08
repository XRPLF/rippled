#pragma once

#include <xrpl/core/Job.h>

#include <chrono>
#include <cstddef>

/**
 * Tuned constants.
 */
/** @{ */
namespace xrpl::rpc::tuning {

/**
 * Represents RPC limit parameter values that have a min, default and max.
 */
struct LimitRange
{
    unsigned int rmin, rDefault, rmax;
};

/**
 * Limits for the account_lines command.
 */
static constexpr LimitRange kAccountLines = {.rmin = 10, .rDefault = 200, .rmax = 400};

/**
 * Limits for the account_channels command.
 */
static constexpr LimitRange kAccountChannels = {.rmin = 10, .rDefault = 200, .rmax = 400};

/**
 * Limits for the account_objects command.
 */
static constexpr LimitRange kAccountObjects = {.rmin = 10, .rDefault = 200, .rmax = 400};

/**
 * Limits for the account_offers command.
 */
static constexpr LimitRange kAccountOffers = {.rmin = 10, .rDefault = 200, .rmax = 400};

/**
 * Limits for the account_tx command.
 */
static constexpr LimitRange kAccountTx = {.rmin = 10, .rDefault = 200, .rmax = 400};

/**
 * Limits for the book_offers command.
 */
static constexpr LimitRange kBookOffers = {.rmin = 1, .rDefault = 60, .rmax = 100};

/**
 * Limits for the no_ripple_check command.
 */
static constexpr LimitRange kNoRippleCheck = {.rmin = 10, .rDefault = 300, .rmax = 400};

/**
 * Limits for the account_nftokens command, in pages.
 */
static constexpr LimitRange kAccountNfTokens = {.rmin = 20, .rDefault = 100, .rmax = 400};

/**
 * Limits for the nft_buy_offers & nft_sell_offers commands.
 */
static constexpr LimitRange kNftOffers = {.rmin = 50, .rDefault = 250, .rmax = 500};

static constexpr int kDefaultAutoFillFeeMultiplier = 10;
static constexpr int kDefaultAutoFillFeeDivisor = 1;
static constexpr int kMaxPathfindsInProgress = 2;
static constexpr int kMaxPathfindJobCount = 50;
static constexpr int kMaxJobQueueClients = 500;
constexpr auto kMaxValidatedLedgerAge = std::chrono::minutes{2};
static constexpr int kMaxRequestSize = 1000000;

/**
 * Maximum number of pages in one response from a binary LedgerData request.
 */
static constexpr int kBinaryPageLength = 2048;

/**
 * Maximum number of pages in one response from a Json LedgerData request.
 */
static constexpr int kJsonPageLength = 256;

/**
 * Maximum number of pages in a LedgerData response.
 */
constexpr int
pageLength(bool isBinary)
{
    return isBinary ? kBinaryPageLength : kJsonPageLength;
}

/**
 * Max paths returned in a path_find / Pathfinder best-path set.
 * Clients receive up to this many alternatives per source asset (no reserved
 * full-liquidity spare slots).
 */
static constexpr int kPathFindMaxPaths = 6;

/**
 * Maximum number of source currencies allowed in a path find request.
 */
static constexpr int kMaxSrcCur = 18;

/**
 * Maximum number of auto source currencies in a path find request
 * (ripple_path_find / explicit legacy). Large for one-shot API completeness.
 */
static constexpr int kMaxAutoSrcCur = 88;

/**
 * Auto source-currency cap for WS path_find subscriptions when the client
 * omits source_currencies. Keeps first/full Pathfinder waves bounded under
 * concurrent sessions (each currency = full graph search + ranking).
 */
static constexpr int kMaxAutoSrcCurSub = 16;

/**
 * Auto source-currency cap while the server is locally loaded.
 */
static constexpr int kMaxAutoSrcCurLoaded = 12;

/**
 * Soft cap on total PathFindTrustLine objects retained in AssetCache.
 * Bounds memory under concurrent path_find sessions. When the budget is
 * exhausted, new chunks are not admitted (no silent floor). Incomplete
 * accounts can grow later as budget frees or on subsequent expand passes.
 *
 * Default for Config::pathFindMaxTotalLines ([path_find] max_total_lines).
 */
static constexpr std::size_t kPathFindMaxTotalLines = 1'000'000;

/**
 * Soft cap on trust lines loaded for a single account (full outgoing set).
 * Default for Config::pathFindMaxLinesPerAccount ([path_find] max_lines_per_account).
 */
static constexpr std::size_t kPathFindMaxLinesPerAccount = 50'000;

/**
 * Trust lines loaded per account per load/expand step for WebSocket path_find.
 * Owner-dir walks are resumable so large accounts fill over successive updates
 * instead of one spike. One-shot ripple_path_find uses LoadScope with
 * kPathFindMaxLinesPerAccount to load the full set in a single reply.
 *
 * Default for Config::pathFindLineChunkSize ([path_find] line_chunk_size).
 */
static constexpr std::size_t kPathFindLineChunkSize = 64;

/**
 * Reuse cached account trust-line vectors across this many ledger advances
 * without reloading. Pathfinding is best-effort; slightly stale lines are OK.
 * Large ledger jumps still force a full clear.
 *
 * Sized so a full 100-session revalidate wave (a few seconds) does not thrash
 * the cache when ledgers close every ~3–4s.
 *
 * Default for Config::pathCacheReuseLedgers ([path_find] cache_reuse_ledgers).
 */
static constexpr std::uint32_t kPathCacheReuseLedgers = 12;

/**
 * Base interval (in ledger closes) between full Pathfinder rediscoveries for an
 * open path_find subscription. Between rediscoveries, updates only re-run
 * rippleCalculate on the previously discovered path set (much cheaper).
 *
 * Actual due ledger is staggered per session:
 *   lastFull + interval + (requestId % interval)
 * so concurrent sessions do not all Pathfinder on the same close.
 * Timed rediscovery is also skipped while the server is locally loaded.
 *
 * Default for Config::pathFullSearchInterval ([path_find] full_search_interval).
 */
static constexpr std::uint32_t kPathFullSearchInterval = 20;

/**
 * Concurrent revalidates for established path_find sessions (not first update).
 * Dispatched as JtPathFindWork jobs (JobTypes limit = kPathFindWorkLimit).
 * Revalidate is mostly independent per request; AssetCache uses a shared_mutex
 * so hits/filters do not fully serialize workers.
 *
 * Sized for ~100 sessions finishing a pure-revalidate wave in well under 1s
 * (target mean path_find update gap ≤2s with periodic mid-close ticks).
 */
static constexpr int kPathSteadyUpdateParallelism = xrpl::kPathFindWorkLimit;

/**
 * Period for open-ledger revalidate-only waves while any path_find session is
 * live. Armed on first session and re-armed after each tick so mean update gap
 * is not hard-bound to the ~4–5s ledger close interval.
 *
 * Each tick is revalidate-only (rippleCalculate on known paths) — never
 * Pathfinder — and is dispatched on JtRpc (not JtUpdatePf) so closed-ledger
 * Pathfinder / first-update waves cannot block the cadence.
 *
 * 500ms targets mean gap ≤1s at 100 sessions when each pure-revalidate wave
 * finishes in well under the period.
 *
 * Default for Config::pathMidCloseDelay ([path_find] mid_close_ms).
 */
static constexpr std::chrono::milliseconds kPathMidCloseDelay{500};

/**
 * After a failed full Pathfinder search, wait this many ledger closes before
 * trying another full search for that session. Prevents unroutable sessions
 * from full-searching every close (the expensive case the cache/revalidate
 * work targets). Revalidate still runs each update; only Pathfinder is gated.
 */
static constexpr std::uint32_t kPathFailedSearchInterval = 5;

/**
 * Max complete paths to liquidity-rank per Pathfinder invocation.
 * Ranking is 1–2 RippleCalc per path; completePaths_ can hold up to 1000.
 * Earlier entries tend to be cheaper path types (gPathTable order).
 */
static constexpr int kPathRankMaxCandidates = 200;

/**
 * Tighter ranking cap when the server is locally loaded.
 */
static constexpr int kPathRankMaxCandidatesLoaded = 80;

}  // namespace xrpl::rpc::tuning
/** @} */
