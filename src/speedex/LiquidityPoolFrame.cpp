#include "speedex/LiquidityPoolFrame.h"
#include "speedex/LiquidityPoolFrameBase.h"

#include "ledger/LedgerTxn.h"

#include "transactions/TransactionUtils.h"

#include <cmath>
#include <stdexcept>
#include <utility>

#include "util/numeric.h"

namespace stellar {

LiquidityPoolFrame::LiquidityPoolFrame(LiquidityPoolFrameBase& baseFrame, AssetPair const& tradingPair)
	: mBaseFrame(baseFrame)
	, mTradingPair (tradingPair)
{
}

LiquidityPoolFrame::operator bool() const {
	return (bool) mBaseFrame;
}

std::pair<int64_t, int64_t> 
LiquidityPoolFrame::getSellBuyAmounts() const {
	if (!mBaseFrame) {
		return std::make_pair<int64_t, int64_t>(0, 0);
	}

	auto [reserveA, reserveB] = mBaseFrame.getReserves();

//	int64_t reserveA = mBaseFrame.mEntry.current().data.liquidityPool().body.constantProduct().reserveA;
//	int64_t reserveB = mBaseFrame.mEntry.current().data.liquidityPool().body.constantProduct().reserveB;

	if (mTradingPair.selling < mTradingPair.buying) {
		return {reserveA, reserveB};
	} else {
		return {reserveB, reserveA};
	}
}

double
LiquidityPoolFrame::getFeeRate() const {
	if (!mBaseFrame) {
		return 0.0;
	}
	return (((double) mBaseFrame.getFee()) / (10000.0));
}

uint32_t
LiquidityPoolFrame::getFixedPointFeeRate() const {
	if (!mBaseFrame) {
		return 0;
	}
	return mBaseFrame.getFee();
}

uint64_t
LiquidityPoolFrame::subtractFeeRateFixedPoint(uint64_t startingPrice) const
{
	uint32_t fee = getFixedPointFeeRate();

	using uint128_t = unsigned __int128;

	uint64_t tax = ((((uint128_t) startingPrice) * fee) / 10000);

	if (tax >= startingPrice) {
		return 0;
	}

	return startingPrice - tax;
}

double
LiquidityPoolFrame::getMinPriceRatio() const {
	auto [sellAmount, buyAmount] = getSellBuyAmounts();
	double feeRate = getFeeRate();
	return (((double) buyAmount) / ((double) sellAmount)) * (1.0 / (1.0 -  feeRate));
}

//returns fraction n/d
std::pair<uint64_t, uint64_t>
LiquidityPoolFrame::getMinPriceRatioFixedPoint() const {
	auto [sellAmount, buyAmount] = getSellBuyAmounts();

	// return (buy / sell) * (1/(1-fee))

	return {buyAmount, subtractFeeRateFixedPoint(sellAmount)};
}

LiquidityPoolFrame::int128_t 
LiquidityPoolFrame::amountOfferedForSaleTimesSellPrice(uint64_t sellPrice, uint64_t buyPrice) const
{
	auto [sellAmount, buyAmount] = getSellBuyAmounts(); // reserves

	using uint128_t = unsigned __int128;

	if (sellAmount == 0) {
		return 0;
	}

	auto [priceN, priceD] = getMinPriceRatioFixedPoint();
	if (priceD == 0) {
		return 0;
	}

	auto priceLT = [] (uint64_t p1n, uint64_t p1d, uint64_t p2n, uint64_t p2d) -> bool
	{
		// p1n/p1d <? p2n/p2d
		return ((uint128_t) p1n) * ((uint128_t) p2d) < ((uint128_t) p2n) * ((uint128_t) p1d);
	};

	if (priceLT(sellPrice, buyPrice, priceN, priceD)) {
		return 0;
	}

	/*

	output trade amounts (not sellprice weighted):

	K = product of reserves (sellAmount * buyAmount)

	sqrt(K) * (1/sqrt(minRatio) - 1/sqrt(offeredRatio))

	but K = sellAmount * buyAmount, and minratio = buyAmount / (sellAmount (1-fee))

	so we get 
	sqrt((sell * buy) * (sell(1-fee)) / buy)
	= sell * sqrt(1-fee)
	= sqrt(sell * sell * (1-fee))
	= sqrt(sellAmount * priceD)

	all told, we get

	sqrt(sell * priceD) - sqrt(sellAmount * buyAmount / (sellPrice / buyPrice))

	Multiplying by sellPrice gives

	sellPrice * sqrt(sellAmount * priceD) - sqrt(sellAmount * sellPrice * buyAmount * buyPrice);
	
	*/

	constexpr auto sqrt = [](uint64_t a, uint64_t b) -> uint64_t {
		// return bigSquareRoot(a, b)
		return (uint64_t) std::ceil(std::pow(((double)a) * ((double) b), 0.5));
	};

	constexpr auto hackyBigSquareRootRoundDown = [=] (uint64_t a, uint64_t b) -> uint64_t {
		uint64_t sqrtRoundUp = sqrt(a, b);
		uint128_t prod = ((uint128_t) a) * ((uint128_t) b);
		uint128_t prodRoundUp = ((uint128_t) sqrtRoundUp) * ((uint128_t) sqrtRoundUp);
		if (prod == prodRoundUp) {
			return sqrtRoundUp;
		}
		return sqrtRoundUp - 1;
	};

	constexpr auto hackyBigSquareRootRoundUp = [=] (uint64_t a, uint64_t b) -> uint64_t {
		return sqrt(a,b);
	};

	// Rounding in this manner underestimates available trade amounts, but this is ok.
	// Better to underestimate than overestimate.
	uint64_t firstTerm = hackyBigSquareRootRoundDown(sellAmount, priceD);
	//std::printf("firstTerm: %llu\n", firstTerm);

	uint64_t secondTermA = hackyBigSquareRootRoundUp(buyAmount, buyPrice);
	uint64_t secondTermB = hackyBigSquareRootRoundUp(sellAmount, sellPrice);

	//std::printf("secondTermA %llu secondTermB %llu\n", secondTermA, secondTermB);

	uint128_t top = static_cast<uint128_t>(sellPrice) * static_cast<uint128_t>(firstTerm);
	uint128_t bot = static_cast<uint128_t>(secondTermA) * static_cast<uint128_t>(secondTermB);

	if (top < bot) {
		return 0;
	}

	uint128_t total = top - bot;

	constexpr uint128_t INT128_MAX = (((uint128_t)1) << 127) - 1;

	if (total > INT128_MAX) {
		total = INT128_MAX;
	}

	// Round again, to ensure we don't have to round against the liquidity pool later.
	// An offer can always offer to sell (amount * price), but the liquidity pool cannot
	// -- it offers some amount * price, but the amount is non integral.
	// The actual offered value is floor(amount) * price.
	uint128_t roundingError = total % static_cast<uint128_t>(sellPrice);

	total -= roundingError;
	return total;
}

int64_t 
LiquidityPoolFrame::amountOfferedForSale(uint64_t sellPrice, uint64_t buyPrice) const {
	//TODO run in fixed point
	double offeredRatio = ((double) sellPrice) / ((double) buyPrice);

	auto [sellAmount, buyAmount] = getSellBuyAmounts();

	if (sellAmount == 0) {
		return 0;
	}

	auto minRatio = getMinPriceRatio();

	if (offeredRatio < minRatio) {
		return 0;
	}

	int128_t k = ((int128_t) sellAmount) * ((int128_t) buyAmount);


	auto invSqrt = [] (double val) {
		return std::pow(val, -0.5);
	};

	auto sqrt = [] (double val) {
		return std::pow(val, 0.5);
	};

	return std::floor(sqrt((double)k) * (invSqrt(minRatio) - invSqrt(offeredRatio)));
}

void 
LiquidityPoolFrame::assertValidSellAmount(int64_t sellAmount, uint64_t sellPrice, uint64_t buyPrice) const {

	int128_t offeredAmt = amountOfferedForSaleTimesSellPrice(sellPrice, buyPrice);
	int128_t queriedAmt = static_cast<int128_t>(sellAmount) * static_cast<int128_t>(sellPrice);

	//std::printf("sellPrice %llu buyPrice %llu amountOfferedForSaleTimesPrice %lf sellAmount %lld queriedAmt %lf\n",
	//		sellPrice,
	//		buyPrice,
	//		(double) offeredAmt,
	//		sellAmount,
	//		(double) queriedAmt);

	if (queriedAmt > offeredAmt) {
		throw std::runtime_error("invalid sell amount!");
	}
	if (sellAmount < 0) {
		throw std::runtime_error("negative sell amount?");
	}
}

// asserts that constant product invariant is preserved
void
LiquidityPoolFrame::assertValidTrade(int64_t sellAmount, int64_t buyAmount, uint64_t sellPrice, uint64_t buyPrice) const {
	assertValidSellAmount(sellAmount, sellPrice, buyPrice);


	auto [oldSellAmount, oldBuyAmount] = getSellBuyAmounts();

	int128_t prevK = ((int128_t) oldSellAmount) * ((int128_t) oldBuyAmount);

	int128_t newK = ((int128_t) (oldSellAmount - sellAmount)) 
		* ((int128_t) (oldBuyAmount + std::floor(((double) buyAmount) * (1.0 - getFeeRate()))));

	if (newK < prevK) {
		throw std::runtime_error("constant product invariant not preserved");
	}
}

SpeedexLiquidityPoolClearingStatus 
LiquidityPoolFrame::doTransfer(int64_t sellAmount, int64_t buyAmount, uint64_t sellPrice, uint64_t buyPrice)
{
	std::printf("doing an lp transfer sellAmount %lld buyAmount %lld\n", sellAmount, buyAmount);
	if (!mBaseFrame) {
		throw std::runtime_error("can't modify nonexistent lp");
	}

	assertValidTrade(sellAmount, buyAmount, sellPrice, buyPrice);

	if (mTradingPair.selling < mTradingPair.buying) {
		mBaseFrame.transfer(-sellAmount, buyAmount);
	//	mBaseFrame.mEntry.current().data.liquidityPool().body.constantProduct().reserveA -= sellAmount;
	//	mBaseFrame.mEntry.current().data.liquidityPool().body.constantProduct().reserveB += buyAmount;
	} else {
		mBaseFrame.transfer(buyAmount, -sellAmount);
		//mBaseFrame.mEntry.current().data.liquidityPool().body.constantProduct().reserveB -= sellAmount;
		//mBaseFrame.mEntry.current().data.liquidityPool().body.constantProduct().reserveA += buyAmount;
	}

	SpeedexLiquidityPoolClearingStatus status;

	status.pool = mBaseFrame.poolID();
    status.soldAsset = mTradingPair.selling;
    status.boughtAsset = mTradingPair.buying;
    status.soldAmount = sellAmount;
    status.boughtAmount = buyAmount;

    return status;
};




} /* stellar */