#include "transactions/CreateSpeedexIOCOfferOpFrame.h"

#include "speedex/SpeedexConfigEntryFrame.h"

#include "ledger/LedgerTxn.h"
#include "ledger/TrustLineWrapper.h"
#include "transactions/TransactionUtils.h"

namespace stellar {

CreateSpeedexIOCOfferOpFrame::CreateSpeedexIOCOfferOpFrame(
	Operation const& op, OperationResult& res, TransactionFrame& parentTx, uint32_t index)
	: OperationFrame(op, res, parentTx)
	, mCreateSpeedexIOCOffer(mOperation.body.createSpeedexIOCOfferOp())
	, mOperationIndex(index)
	{

	}


bool
CreateSpeedexIOCOfferOpFrame::checkMalformed() {
	if (mCreateSpeedexIOCOffer.sellAmount <= 0) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_MALFORMED);
		return false;
	}
	auto price = mCreateSpeedexIOCOffer.minPrice;
	if (price.n <= 0 || price.d <= 0) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_MALFORMED);
		return false;
	}
	return true;
}

bool
CreateSpeedexIOCOfferOpFrame::checkValidAssetPair(AbstractLedgerTxn& ltx) {
	auto speedexConfig = stellar::loadSpeedexConfigSnapshot(ltx);
	if (!speedexConfig) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_NO_SPEEDEX_CONFIG);
		return false;
	}

	AssetPair tradingPair {
		.buying = mCreateSpeedexIOCOffer.buyAsset,
		.selling = mCreateSpeedexIOCOffer.sellAsset
	};

	if (!speedexConfig.isValidAssetPair(tradingPair)) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_INVALID_TRADING_PAIR);
		return false;
	}

	if (!stellar::isCommutativeTxEnabledAsset(ltx, tradingPair.selling)) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_MALFORMED);
		return false;
	}
	
	if (!stellar::isCommutativeTxEnabledAsset(ltx, tradingPair.buying)) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_MALFORMED);
		return false;
	}
	return true;
}

bool 
CreateSpeedexIOCOfferOpFrame::doApply(AbstractLedgerTxn& ltx)
{
	if (!checkValidAssetPair(ltx)) {
		return false;
	}

	if (!checkMalformed()) {
		return false;
	}

	auto price = mCreateSpeedexIOCOffer.minPrice;
	auto amount = mCreateSpeedexIOCOffer.sellAmount;

	IOCOffer offer(amount, price, getSourceID(), mParentTx.getSeqNum(), mOperationIndex);

	AssetPair tradingPair {
		.buying = mCreateSpeedexIOCOffer.buyAsset,
		.selling = mCreateSpeedexIOCOffer.sellAsset
	};

	ltx.addSpeedexIOCOffer(tradingPair, offer);
	
	return true;
}
bool 
CreateSpeedexIOCOfferOpFrame::doCheckValid(uint32_t ledgerVersion)
{
	return checkMalformed();
}

bool 
CreateSpeedexIOCOfferOpFrame::doAddCommutativityRequirements(AbstractLedgerTxn& ltx, TransactionCommutativityRequirements& reqs) {

	if (!checkValidAssetPair(ltx)) {
		return false;
	}

	if (!reqs.checkTrustLine(ltx, getSourceID(), mCreateSpeedexIOCOffer.buyAsset)) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_MALFORMED);
		return false;
	}

	if (!reqs.checkTrustLine(ltx, getSourceID(), mCreateSpeedexIOCOffer.sellAsset)) {
		innerResult().code(CREATE_SPEEDEX_IOC_OFFER_MALFORMED);
		return false;
	}

	doAddCommutativityRequirementsUnconditional(reqs);
    return true;
}

void
CreateSpeedexIOCOfferOpFrame::doAddCommutativityRequirementsUnconditional(TransactionCommutativityRequirements& reqs) const
{
	reqs.addAssetRequirement(getSourceID(), mCreateSpeedexIOCOffer.sellAsset, mCreateSpeedexIOCOffer.sellAmount);
}


void
CreateSpeedexIOCOfferOpFrame::insertLedgerKeysToPrefetch(UnorderedSet<LedgerKey>& keys) const {

	auto sourceID = getSourceID();

	auto trustLineKeyGen = [&] (Asset asset) {
		if (asset.type() != ASSET_TYPE_NATIVE) {
			keys.emplace(trustlineKey(sourceID, asset));
		}
	};

	trustLineKeyGen(mCreateSpeedexIOCOffer.sellAsset);
	trustLineKeyGen(mCreateSpeedexIOCOffer.buyAsset);
}


} /* stellar */