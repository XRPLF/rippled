# XLS-AMM-Concentrated-Liquidity: Automated Market Maker Concentrated Liquidity

**Title:** Automated Market Maker Concentrated Liquidity  
**Revision:** 1 (2025-01-27)  
**Type:** Draft  
**Author:**  
    Luke Judges, Ripple [lukejudges](https://github.com/LJ-XRPL)  
**Affiliation:** XRPL Community  

This proposal introduces concentrated liquidity functionality to the XRP Ledger's Automated Market Maker (AMM) system, enabling liquidity providers to focus their capital within specific price ranges. This enhancement significantly improves capital efficiency and provides more granular control over liquidity provision, similar to Uniswap V3's concentrated liquidity model.

## 1. Implementation

This amendment extends the existing AMM functionality to support concentrated liquidity positions, allowing liquidity providers to specify price ranges for their liquidity and earn fees from trades within those ranges.

### 1.1. Overview of Concentrated Liquidity

#### 1.1.1. Core Concepts

**Price Ranges:** Liquidity providers specify upper and lower price bounds (tickLower and tickUpper) within which their liquidity is active.

**Tick System:** Prices are represented as integer ticks where `price = 1.0001^tick`. This provides precise price granularity while maintaining computational efficiency.

**Fee Tiers:** Multiple fee tiers are supported to accommodate different risk profiles and trading volumes:
- 0.01% fee tier (tick spacing: 1)
- 0.05% fee tier (tick spacing: 10)  
- 0.3% fee tier (tick spacing: 60)
- 1.0% fee tier (tick spacing: 200)

**Capital Efficiency:** Liquidity is only active within the specified price range, allowing providers to achieve higher capital efficiency compared to traditional AMM liquidity.

#### 1.1.2. Key Components

**Concentrated Liquidity Position:** A ledger object representing a liquidity provider's position within a specific price range.

**Concentrated Liquidity Tick:** A ledger object tracking liquidity and fee information at specific price levels.

**Tick Crossing:** The process of executing trades that move the current price across initialized ticks, updating liquidity and fee growth.

**Fee Growth Tracking:** Global and position-specific tracking of accumulated fees for accurate fee distribution.

### 1.2. New Transaction Types

#### 1.2.1. AMMConcentratedCreate

Creates a new AMM with concentrated liquidity support.

**Fields:**

| Field | Required? | JSON Type | Internal Type | Description |
|-------|-----------|-----------|---------------|-------------|
| Amount | Yes | Object | Amount | Initial liquidity amount for both assets |
| Amount2 | Yes | Object | Amount | Second asset amount for initial liquidity |
| TradingFee | Yes | Number | UInt16 | Trading fee in basis points (must match supported fee tiers) |
| TickSpacing | Yes | Number | UInt16 | Tick spacing for the AMM (must match fee tier) |

**Failure Conditions:**

- **Invalid Fee Tier:** If `TradingFee` does not match a supported fee tier, transaction fails with `tecAMM_INVALID_FEE_TIER`
- **Invalid Tick Spacing:** If `TickSpacing` does not match the fee tier's required spacing, transaction fails with `tecAMM_INVALID_TICK_SPACING`
- **Insufficient Liquidity:** If initial liquidity is below minimum threshold, transaction fails with `tecAMM_INSUFFICIENT_LIQUIDITY`
- **Invalid Amounts:** If either amount is zero or negative, transaction fails with `tecAMM_INVALID_AMOUNTS`

**State Changes:**

- **AMM Object Creation:** Creates an AMM ledger object with concentrated liquidity fields
- **Initial Tick Setup:** Initializes the current tick and sqrt price
- **Directory Management:** Adds the AMM to appropriate directories

#### 1.2.2. AMMConcentratedDeposit

Deposits liquidity into a concentrated liquidity position.

**Fields:**

| Field | Required? | JSON Type | Internal Type | Description |
|-------|-----------|-----------|---------------|-------------|
| AMMID | Yes | String | Hash256 | The AMM identifier |
| Amount0Max | Yes | Object | Amount | Maximum amount of first asset to deposit |
| Amount1Max | Yes | Object | Amount | Maximum amount of second asset to deposit |
| TickLower | Yes | Number | UInt32 | Lower tick boundary for the position |
| TickUpper | Yes | Number | UInt32 | Upper tick boundary for the position |
| LiquidityMin | Yes | Object | Amount | Minimum liquidity to receive |

**Failure Conditions:**

- **Invalid Tick Range:** If `TickLower >= TickUpper`, transaction fails with `tecAMM_INVALID_TICK_RANGE`
- **Invalid Tick Spacing:** If ticks are not valid for the AMM's fee tier, transaction fails with `tecAMM_INVALID_TICK_SPACING`
- **Insufficient Balance:** If account lacks sufficient balance, transaction fails with `tecUNFUNDED`
- **Position Already Exists:** If position with same parameters exists, transaction fails with `tecAMM_POSITION_EXISTS`

**State Changes:**

- **Position Creation:** Creates a concentrated liquidity position ledger object
- **Asset Transfers:** Transfers assets from account to AMM
- **Liquidity Calculation:** Calculates and stores liquidity amount based on current price and tick range

#### 1.2.3. AMMConcentratedWithdraw

Withdraws liquidity from a concentrated liquidity position.

**Fields:**

| Field | Required? | JSON Type | Internal Type | Description |
|-------|-----------|-----------|---------------|-------------|
| AMMID | Yes | String | Hash256 | The AMM identifier |
| TickLower | Yes | Number | UInt32 | Lower tick boundary of the position |
| TickUpper | Yes | Number | UInt32 | Upper tick boundary of the position |
| PositionNonce | Yes | Number | UInt32 | Position nonce for uniqueness |
| Liquidity | Yes | Object | Amount | Amount of liquidity to withdraw |
| Amount0Min | Yes | Object | Amount | Minimum amount of first asset to receive |
| Amount1Min | Yes | Object | Amount | Minimum amount of second asset to receive |

**Failure Conditions:**

- **Position Not Found:** If position does not exist, transaction fails with `tecAMM_POSITION_NOT_FOUND`
- **Insufficient Liquidity:** If position has insufficient liquidity, transaction fails with `tecAMM_INSUFFICIENT_LIQUIDITY`
- **Slippage Protection:** If received amounts are below minimums, transaction fails with `tecAMM_SLIPPAGE_EXCEEDED`

**State Changes:**

- **Position Update:** Updates position's liquidity amount
- **Asset Transfers:** Transfers assets from AMM to account
- **Tick Updates:** Updates liquidity at affected ticks

#### 1.2.4. AMMConcentratedCollect

Collects accumulated fees from a concentrated liquidity position.

**Fields:**

| Field | Required? | JSON Type | Internal Type | Description |
|-------|-----------|-----------|---------------|-------------|
| AMMID | Yes | String | Hash256 | The AMM identifier |
| TickLower | Yes | Number | UInt32 | Lower tick boundary of the position |
| TickUpper | Yes | Number | UInt32 | Upper tick boundary of the position |
| PositionNonce | Yes | Number | UInt32 | Position nonce for uniqueness |
| CollectFees | Yes | Boolean | UInt8 | Whether to collect fees |

**Failure Conditions:**

- **Position Not Found:** If position does not exist, transaction fails with `tecAMM_POSITION_NOT_FOUND`
- **No Fees Available:** If no fees are available to collect, transaction fails with `tecAMM_NO_FEES_AVAILABLE`

**State Changes:**

- **Fee Collection:** Transfers accumulated fees to position owner
- **Position Update:** Resets fee tracking fields in position

### 1.3. Ledger Objects

#### 1.3.1. ConcentratedLiquidityPosition Ledger Object

**Type:** `ltCONCENTRATED_LIQUIDITY_POSITION`

| Field Name | JSON Type | Internal Type | Description |
|------------|-----------|---------------|-------------|
| Owner | String | AccountID | Position owner account |
| AMMID | String | Hash256 | Associated AMM identifier |
| TickLower | Number | UInt32 | Lower tick boundary |
| TickUpper | Number | UInt32 | Upper tick boundary |
| Liquidity | Object | Amount | Current liquidity amount |
| FeeGrowthInside0LastX128 | Object | Amount | Last fee growth for asset 0 |
| FeeGrowthInside1LastX128 | Object | Amount | Last fee growth for asset 1 |
| TokensOwed0 | Object | Amount | Unclaimed fees for asset 0 |
| TokensOwed1 | Object | Amount | Unclaimed fees for asset 1 |
| PositionNonce | Number | UInt32 | Position nonce for uniqueness |

#### 1.3.2. ConcentratedLiquidityTick Ledger Object

**Type:** `ltCONCENTRATED_LIQUIDITY_TICK`

| Field Name | JSON Type | Internal Type | Description |
|------------|-----------|---------------|-------------|
| TickIndex | Number | UInt32 | Tick index |
| LiquidityGross | Object | Amount | Total liquidity at this tick |
| LiquidityNet | Object | Amount | Net liquidity change |
| FeeGrowthOutside0X128 | Object | Amount | Fee growth outside for asset 0 |
| FeeGrowthOutside1X128 | Object | Amount | Fee growth outside for asset 1 |
| TickInitialized | Boolean | UInt8 | Whether tick is initialized |

#### 1.3.3. Enhanced AMM Ledger Object

The existing AMM ledger object is extended with concentrated liquidity fields:

| Field Name | JSON Type | Internal Type | Description |
|------------|-----------|---------------|-------------|
| CurrentTick | Number | UInt32 | Current tick index |
| SqrtPriceX64 | Number | UInt64 | Current sqrt price in Q64.64 format |
| AggregatedLiquidity | Object | Amount | Total aggregated liquidity |
| FeeGrowthGlobal0X128 | Object | Amount | Global fee growth for asset 0 |
| FeeGrowthGlobal1X128 | Object | Amount | Global fee growth for asset 1 |
| TickSpacing | Number | UInt16 | Tick spacing for this AMM |

### 1.4. Payment Engine Integration

#### 1.4.1. AMMConLiquidityStep

A new payment step that routes payments through concentrated liquidity pools.

**Key Features:**
- **Price Impact Calculation:** Calculates price impact based on current liquidity and trade size
- **Tick Crossing:** Handles trades that cross multiple ticks
- **Fee Distribution:** Updates fee growth for affected positions
- **Liquidity Updates:** Updates liquidity at crossed ticks

**Integration Points:**
- **Pathfinding:** Integrated into the payment engine's pathfinding algorithm
- **Quality Calculation:** Dynamic quality calculation based on current price and liquidity
- **Fee Tracking:** Automatic fee growth updates during trades

#### 1.4.2. Tick Crossing Logic

**Step-by-Step Process:**
1. **Target Price Calculation:** Determine the target price for the trade
2. **Tick Scanning:** Find the next initialized tick in the direction of the trade
3. **Swap Execution:** Execute the swap up to the next tick boundary
4. **Fee Updates:** Update fee growth for all positions at the crossed tick
5. **Liquidity Updates:** Update liquidity at the crossed tick
6. **Position Updates:** Update all positions that span the crossed tick
7. **Repeat:** Continue until the target price is reached or liquidity is exhausted

### 1.5. Fee Mechanism

#### 1.5.1. Fee Calculation

**Fee Collection:** Fees are collected in the asset being sold and distributed proportionally to liquidity providers.

**Fee Growth Tracking:**
- **Global Fee Growth:** Tracks total fees collected by the AMM
- **Position-Specific Tracking:** Each position tracks its last fee growth update
- **Fee Calculation:** Fees owed = (current fee growth - last fee growth) × position liquidity

#### 1.5.2. Fee Distribution

**Proportional Distribution:** Fees are distributed proportionally to liquidity providers based on their liquidity share.

**Real-Time Updates:** Fee growth is updated in real-time during trades, ensuring accurate fee distribution.

**Collection Mechanism:** Position owners can collect accumulated fees using the `AMMConcentratedCollect` transaction.

### 1.6. Price Calculations

#### 1.6.1. Q64.64 Fixed Point Arithmetic

**Price Representation:** Prices are represented as 64-bit fixed-point numbers in Q64.64 format.

**Conversion Functions:**
- `tickToSqrtPriceX64(tick)`: Converts tick to sqrt price
- `sqrtPriceX64ToTick(sqrtPriceX64)`: Converts sqrt price to tick

**Precision:** Provides high precision for price calculations while maintaining computational efficiency.

#### 1.6.2. Liquidity Calculations

**Liquidity for Amounts:** Calculates the liquidity required for a given amount of assets at a specific price range.

**Amounts for Liquidity:** Calculates the amounts of assets that can be withdrawn for a given liquidity amount.

### 1.7. Security Considerations

#### 1.7.1. Input Validation

**Tick Range Validation:** Ensures tick ranges are valid and within bounds.

**Liquidity Validation:** Validates liquidity amounts to prevent overflow and underflow.

**Fee Tier Validation:** Ensures fee tiers and tick spacing are consistent.

#### 1.7.2. Overflow Protection

**Price Calculations:** All price calculations include overflow checks.

**Liquidity Updates:** Liquidity updates are protected against overflow and underflow.

**Fee Growth:** Fee growth calculations include precision and overflow protection.

#### 1.7.3. Atomic Operations

**Position Updates:** All position updates are atomic to maintain consistency.

**Tick Crossing:** Tick crossing operations are atomic to prevent partial updates.

**Fee Collection:** Fee collection is atomic to prevent double-spending.

### 1.8. RPC Extensions

#### 1.8.1. Enhanced amm_info

The existing `amm_info` RPC call is extended to include concentrated liquidity information:

**New Fields:**
- `concentrated_liquidity_enabled`: Boolean indicating if concentrated liquidity is enabled
- `current_tick`: Current tick index
- `sqrt_price_x64`: Current sqrt price in Q64.64 format
- `tick_spacing`: Tick spacing for this AMM
- `aggregated_liquidity`: Total aggregated liquidity
- `fee_growth_global_0_x128`: Global fee growth for asset 0
- `fee_growth_global_1_x128`: Global fee growth for asset 1
- `positions`: Array of active concentrated liquidity positions

### 1.9. Error Codes

**New Transaction Error Codes:**

| Error Code | Description |
|------------|-------------|
| `tecAMM_INVALID_TICK_RANGE` | Invalid tick range (lower >= upper) |
| `tecAMM_INSUFFICIENT_LIQUIDITY` | Insufficient liquidity for operation |
| `tecAMM_POSITION_NOT_FOUND` | Concentrated liquidity position not found |
| `tecAMM_TICK_NOT_INITIALIZED` | Tick is not initialized |
| `tecAMM_INVALID_POSITION_NONCE` | Invalid position nonce |
| `tecAMM_INVALID_FEE_TIER` | Invalid fee tier for AMM |
| `tecAMM_INVALID_TICK_SPACING` | Invalid tick spacing for fee tier |
| `tecAMM_POSITION_EXISTS` | Position already exists |
| `tecAMM_NO_FEES_AVAILABLE` | No fees available to collect |
| `tecAMM_SLIPPAGE_EXCEEDED` | Slippage exceeds maximum allowed |

### 1.10. Feature Amendment

**Amendment Name:** `featureAMMConcentratedLiquidity`

**Description:** Enables concentrated liquidity functionality for Automated Market Makers.

**Default State:** Disabled (requires activation through amendment process)

**Vote Behavior:** DefaultNo (requires explicit support to activate)

### 1.11. Backward Compatibility

**Existing AMMs:** Existing AMMs continue to function normally without concentrated liquidity.

**Gradual Migration:** Liquidity providers can gradually migrate to concentrated liquidity as needed.

**Feature Flag:** Concentrated liquidity is controlled by a feature amendment, ensuring backward compatibility.

### 1.12. Performance Considerations

**Tick Scanning:** Efficient tick scanning algorithms minimize computational overhead.

**Position Registry:** In-memory position registry provides fast position lookups.

**Batch Updates:** Tick crossing operations are batched to minimize ledger updates.

**Gas Optimization:** Optimized calculations reduce computational costs.

### 1.13. Future Considerations

**Dynamic Fee Tiers:** Future versions may support dynamic fee tiers based on market conditions.

**Position NFTs:** Positions could be represented as NFTs for enhanced transferability.

**Advanced Strategies:** Support for more complex liquidity provision strategies.

**Cross-Chain Integration:** Integration with cross-chain liquidity protocols.

**Governance:** Community governance mechanisms for fee tier adjustments.

### 1.14. Testing Strategy

**Unit Tests:** Comprehensive unit tests for all new transaction types and functions.

**Integration Tests:** Integration tests for payment engine integration.

**Performance Tests:** Performance tests for tick crossing and position management.

**Security Tests:** Security tests for overflow protection and atomic operations.

**Compatibility Tests:** Tests for backward compatibility with existing AMMs.

---

**Note:** This is an experimental implementation and should be thoroughly tested before production use. The feature amendment provides a safety mechanism to disable the functionality if issues are discovered.
