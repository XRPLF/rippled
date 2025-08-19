---
title: Automated Market Maker Concentrated Liquidity
type: draft
description: Extends XRP Ledger AMM functionality with concentrated liquidity positions for improved capital efficiency
author: Luke Judges, XRPL Community (@lukejudges)
requires: XLS-0030
core_protocol_changes_required: true
---

> **Note**: This is a proposal for an XLS (XRP Ledger Standard) specification. This document outlines the technical design and requirements for implementing concentrated liquidity functionality in the XRP Ledger's AMM system. The goal is to establish a community-driven standard that can be discussed, refined, and potentially adopted through the XRPL amendment process. This is not an immediate implementation proposal but rather a specification for future consideration.

## Abstract

This specification introduces concentrated liquidity functionality to the XRP Ledger's Automated Market Maker (AMM) system, enabling liquidity providers to focus their capital within specific price ranges. This enhancement significantly improves capital efficiency and provides more granular control over liquidity provision, similar to Uniswap V3's concentrated liquidity model. The specification defines new transaction types, ledger objects, and payment engine integration for managing concentrated liquidity positions with precise tick-based pricing and fee distribution.

## Motivation

Traditional AMM liquidity provision requires capital to be distributed across the entire price range, leading to inefficient capital utilization. Concentrated liquidity allows providers to focus their capital within specific price ranges where they expect trading activity, achieving significantly higher capital efficiency. This enables:

- **Higher Capital Efficiency**: Liquidity providers can achieve the same trading volume with less capital
- **Granular Control**: Providers can target specific price ranges based on market conditions
- **Improved Fee Generation**: Concentrated capital leads to higher fee generation per unit of capital
- **Risk Management**: Providers can avoid providing liquidity in unfavorable price ranges

The implementation builds upon the existing AMM infrastructure (XLS-0030) while maintaining backward compatibility and integrating seamlessly with the current payment engine.

## Specification

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be interpreted as described in RFC 2119 and RFC 8174.

### Core Concepts

#### Tick System
Prices are represented as integer ticks where `price = 1.0001^tick`. This provides precise price granularity while maintaining computational efficiency.

#### Fee Tiers
Multiple fee tiers are supported to accommodate different risk profiles and trading volumes:
- 0.01% fee tier (tick spacing: 1)
- 0.05% fee tier (tick spacing: 10)  
- 0.3% fee tier (tick spacing: 60)
- 1.0% fee tier (tick spacing: 200)

#### Q64.64 Fixed Point Arithmetic
Prices are represented as 64-bit fixed-point numbers in Q64.64 format for high precision calculations.

### New Transaction Types

#### **`AMMConcentratedCreate`** transaction

Creates a new AMM with concentrated liquidity support.

##### Fields

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `Amount` | :heavy_check_mark: | Object | Amount | Initial liquidity amount for both assets |
| `Amount2` | :heavy_check_mark: | Object | Amount | Second asset amount for initial liquidity |
| `TradingFee` | :heavy_check_mark: | Number | UInt16 | Trading fee in basis points (must match supported fee tiers) |
| `TickSpacing` | :heavy_check_mark: | Number | UInt16 | Tick spacing for the AMM (must match fee tier) |

**Failure Conditions:**
- If `TradingFee` does not match a supported fee tier, transaction fails with `tecAMM_INVALID_FEE_TIER`
- If `TickSpacing` does not match the fee tier's required spacing, transaction fails with `tecAMM_INVALID_TICK_SPACING`
- If initial liquidity is below minimum threshold, transaction fails with `tecAMM_INSUFFICIENT_LIQUIDITY`
- If either amount is zero or negative, transaction fails with `tecAMM_INVALID_AMOUNTS`

#### **`AMMConcentratedDeposit`** transaction

Deposits liquidity into a concentrated liquidity position.

##### Fields

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `AMMID` | :heavy_check_mark: | String | Hash256 | The AMM identifier |
| `Amount0Max` | :heavy_check_mark: | Object | Amount | Maximum amount of first asset to deposit |
| `Amount1Max` | :heavy_check_mark: | Object | Amount | Maximum amount of second asset to deposit |
| `TickLower` | :heavy_check_mark: | Number | UInt32 | Lower tick boundary for the position |
| `TickUpper` | :heavy_check_mark: | Number | UInt32 | Upper tick boundary for the position |
| `LiquidityMin` | :heavy_check_mark: | Object | Amount | Minimum liquidity to receive |

**Failure Conditions:**
- If `TickLower >= TickUpper`, transaction fails with `tecAMM_INVALID_TICK_RANGE`
- If ticks are not valid for the AMM's fee tier, transaction fails with `tecAMM_INVALID_TICK_SPACING`
- If account lacks sufficient balance, transaction fails with `tecUNFUNDED`
- If position with same parameters exists, transaction fails with `tecAMM_POSITION_EXISTS`

#### **`AMMConcentratedWithdraw`** transaction

Withdraws liquidity from a concentrated liquidity position.

##### Fields

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `AMMID` | :heavy_check_mark: | String | Hash256 | The AMM identifier |
| `TickLower` | :heavy_check_mark: | Number | UInt32 | Lower tick boundary of the position |
| `TickUpper` | :heavy_check_mark: | Number | UInt32 | Upper tick boundary of the position |
| `PositionNonce` | :heavy_check_mark: | Number | UInt32 | Position nonce for uniqueness |
| `Liquidity` | :heavy_check_mark: | Object | Amount | Amount of liquidity to withdraw |
| `Amount0Min` | :heavy_check_mark: | Object | Amount | Minimum amount of first asset to receive |
| `Amount1Min` | :heavy_check_mark: | Object | Amount | Minimum amount of second asset to receive |

**Failure Conditions:**
- If position does not exist, transaction fails with `tecAMM_POSITION_NOT_FOUND`
- If position has insufficient liquidity, transaction fails with `tecAMM_INSUFFICIENT_LIQUIDITY`
- If received amounts are below minimums, transaction fails with `tecAMM_SLIPPAGE_EXCEEDED`

#### **`AMMConcentratedCollect`** transaction

Collects accumulated fees from a concentrated liquidity position.

##### Fields

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `AMMID` | :heavy_check_mark: | String | Hash256 | The AMM identifier |
| `TickLower` | :heavy_check_mark: | Number | UInt32 | Lower tick boundary of the position |
| `TickUpper` | :heavy_check_mark: | Number | UInt32 | Upper tick boundary of the position |
| `PositionNonce` | :heavy_check_mark: | Number | UInt32 | Position nonce for uniqueness |
| `CollectFees` | :heavy_check_mark: | Boolean | UInt8 | Whether to collect fees |

**Failure Conditions:**
- If position does not exist, transaction fails with `tecAMM_POSITION_NOT_FOUND`
- If no fees are available to collect, transaction fails with `tecAMM_NO_FEES_AVAILABLE`

### New Ledger Objects

#### **`ConcentratedLiquidityPosition`** ledger entry

Represents a liquidity provider's position within a specific price range.

##### Fields

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `Owner` | :heavy_check_mark: | String | AccountID | Position owner account |
| `AMMID` | :heavy_check_mark: | String | Hash256 | Associated AMM identifier |
| `TickLower` | :heavy_check_mark: | Number | UInt32 | Lower tick boundary |
| `TickUpper` | :heavy_check_mark: | Number | UInt32 | Upper tick boundary |
| `Liquidity` | :heavy_check_mark: | Object | Amount | Current liquidity amount |
| `FeeGrowthInside0LastX128` | :heavy_check_mark: | Object | Amount | Last fee growth for asset 0 |
| `FeeGrowthInside1LastX128` | :heavy_check_mark: | Object | Amount | Last fee growth for asset 1 |
| `TokensOwed0` | :heavy_check_mark: | Object | Amount | Unclaimed fees for asset 0 |
| `TokensOwed1` | :heavy_check_mark: | Object | Amount | Unclaimed fees for asset 1 |
| `PositionNonce` | :heavy_check_mark: | Number | UInt32 | Position nonce for uniqueness |

#### **`ConcentratedLiquidityTick`** ledger entry

Tracks liquidity and fee information at specific price levels.

##### Fields

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `TickIndex` | :heavy_check_mark: | Number | UInt32 | Tick index |
| `LiquidityGross` | :heavy_check_mark: | Object | Amount | Total liquidity at this tick |
| `LiquidityNet` | :heavy_check_mark: | Object | Amount | Net liquidity change |
| `FeeGrowthOutside0X128` | :heavy_check_mark: | Object | Amount | Fee growth outside for asset 0 |
| `FeeGrowthOutside1X128` | :heavy_check_mark: | Object | Amount | Fee growth outside for asset 1 |
| `TickInitialized` | :heavy_check_mark: | Boolean | UInt8 | Whether tick is initialized |

### Enhanced AMM Ledger Object

The existing AMM ledger object is extended with concentrated liquidity fields:

| Field Name | Required? | JSON Type | Internal Type | Description |
|------------|:---------:|:---------:|:-------------:|-------------|
| `CurrentTick` | :heavy_check_mark: | Number | UInt32 | Current tick index |
| `SqrtPriceX64` | :heavy_check_mark: | Number | UInt64 | Current sqrt price in Q64.64 format |
| `AggregatedLiquidity` | :heavy_check_mark: | Object | Amount | Total aggregated liquidity |
| `FeeGrowthGlobal0X128` | :heavy_check_mark: | Object | Amount | Global fee growth for asset 0 |
| `FeeGrowthGlobal1X128` | :heavy_check_mark: | Object | Amount | Global fee growth for asset 1 |
| `TickSpacing` | :heavy_check_mark: | Number | UInt16 | Tick spacing for this AMM |

### Payment Engine Integration

#### AMMConLiquidityStep

A new payment step that routes payments through concentrated liquidity pools.

**Key Features:**
- Price impact calculation based on current liquidity and trade size
- Tick crossing for trades that move across multiple price levels
- Fee distribution updates for affected positions
- Liquidity updates at crossed ticks

**Integration Points:**
- Integrated into the payment engine's pathfinding algorithm
- Dynamic quality calculation based on current price and liquidity
- Automatic fee growth updates during trades

#### Tick Crossing Logic

**Step-by-Step Process:**
1. Target price calculation for the trade
2. Tick scanning to find the next initialized tick
3. Swap execution up to the next tick boundary
4. Fee updates for all positions at the crossed tick
5. Liquidity updates at the crossed tick
6. Position updates for all positions spanning the crossed tick
7. Repeat until target price is reached or liquidity is exhausted

### Fee Mechanism

#### Fee Calculation
Fees are collected in the asset being sold and distributed proportionally to liquidity providers.

#### Fee Growth Tracking
- **Global Fee Growth**: Tracks total fees collected by the AMM
- **Position-Specific Tracking**: Each position tracks its last fee growth update
- **Fee Calculation**: Fees owed = (current fee growth - last fee growth) × position liquidity

### Price Calculations

#### Conversion Functions
- `tickToSqrtPriceX64(tick)`: Converts tick to sqrt price
- `sqrtPriceX64ToTick(sqrtPriceX64)`: Converts sqrt price to tick

#### Liquidity Calculations
- **Liquidity for Amounts**: Calculates liquidity required for given amounts at a price range
- **Amounts for Liquidity**: Calculates amounts that can be withdrawn for given liquidity

### Error Codes

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

### Feature Amendment

**Amendment Name:** `featureAMMConcentratedLiquidity`

**Description:** Enables concentrated liquidity functionality for Automated Market Makers.

**Default State:** Disabled (requires activation through amendment process)

**Vote Behavior:** DefaultNo (requires explicit support to activate)

## Rationale

The design follows established patterns from successful concentrated liquidity implementations like Uniswap V3, while adapting them to the XRP Ledger's architecture and constraints. Key design decisions include:

**Tick System**: Using a 1.0001 multiplier provides sufficient price granularity while maintaining computational efficiency. The tick spacing system allows for different fee tiers with appropriate liquidity distribution.

**Q64.64 Fixed Point**: This format provides high precision for price calculations while avoiding floating-point arithmetic issues and maintaining deterministic behavior.

**Position Registry**: An in-memory registry provides fast position lookups while maintaining consistency with the ledger state.

**Fee Growth Tracking**: Global and position-specific fee growth tracking ensures accurate fee distribution without requiring complex calculations during trades.

**Backward Compatibility**: The design maintains full backward compatibility with existing AMMs, allowing for gradual migration.

## Backwards Compatibility

No backward compatibility issues found. The implementation:

- Maintains all existing AMM functionality
- Uses feature amendment for activation
- Preserves existing transaction types and ledger objects
- Allows existing AMMs to continue operating normally
- Provides gradual migration path for liquidity providers

## Test Cases

### Basic Functionality Tests
1. **AMM Creation**: Create AMM with valid fee tier and tick spacing
2. **Position Creation**: Deposit liquidity into valid tick range
3. **Position Withdrawal**: Withdraw liquidity with proper asset calculation
4. **Fee Collection**: Collect accumulated fees from position

### Edge Cases
1. **Invalid Tick Range**: Attempt to create position with lower >= upper tick
2. **Invalid Fee Tier**: Attempt to create AMM with unsupported fee tier
3. **Insufficient Liquidity**: Attempt to withdraw more liquidity than available
4. **Tick Crossing**: Execute trades that cross multiple initialized ticks

### Integration Tests
1. **Payment Engine**: Route payments through concentrated liquidity pools
2. **Fee Distribution**: Verify accurate fee distribution across positions
3. **Price Impact**: Calculate and verify price impact for various trade sizes

## Invariants

1. **Tick Range Validity**: `tickLower < tickUpper` for all positions
2. **Liquidity Conservation**: Total liquidity at any tick equals sum of all positions spanning that tick
3. **Fee Growth Consistency**: Global fee growth equals sum of fee growth outside for all ticks
4. **Price Monotonicity**: Price increases monotonically with tick index
5. **Position Uniqueness**: Each position is uniquely identified by (owner, tickLower, tickUpper, nonce)
6. **Asset Conservation**: Sum of all position assets plus AMM balances equals total supply
7. **Fee Accuracy**: Collected fees equal distributed fees plus unclaimed fees

## Reference Implementation

A complete reference implementation is available in the `rippled` codebase, demonstrating:

- Full transaction processing logic
- Payment engine integration
- Comprehensive test suite
- Security audit and fixes
- Performance optimizations

The implementation includes all transaction types, ledger objects, and integration points specified in this document.

## Security Considerations

### Input Validation
- **Tick Range Validation**: Ensures tick ranges are valid and within bounds
- **Liquidity Validation**: Validates liquidity amounts to prevent overflow and underflow
- **Fee Tier Validation**: Ensures fee tiers and tick spacing are consistent

### Overflow Protection
- **Price Calculations**: All price calculations include overflow checks
- **Liquidity Updates**: Liquidity updates are protected against overflow and underflow
- **Fee Growth**: Fee growth calculations include precision and overflow protection

### Atomic Operations
- **Position Updates**: All position updates are atomic to maintain consistency
- **Tick Crossing**: Tick crossing operations are atomic to prevent partial updates
- **Fee Collection**: Fee collection is atomic to prevent double-spending

### Attack Vectors
- **Front-Running**: Protected through atomic operations and proper ordering
- **Sandwich Attacks**: Mitigated through slippage protection and price impact calculations
- **Liquidity Manipulation**: Prevented through proper validation and invariant checks

### Economic Security
- **Capital Efficiency**: Concentrated liquidity improves capital efficiency without compromising security
- **Fee Distribution**: Proportional fee distribution prevents manipulation
- **Slippage Protection**: Minimum amount requirements protect against unfavorable trades

The implementation has undergone security review and includes comprehensive testing to validate all security measures.
