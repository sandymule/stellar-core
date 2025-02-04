// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/TransactionUtils.h"
#include "crypto/SHA.h"
#include "crypto/SecretKey.h"
#include "ledger/InternalLedgerEntry.h"
#include "ledger/LedgerTxn.h"
#include "ledger/LedgerTxnEntry.h"
#include "ledger/LedgerTxnHeader.h"
#include "ledger/TrustLineWrapper.h"
#include "speedex/SpeedexConfigEntryFrame.h"
#include "transactions/OfferExchange.h"
#include "transactions/SponsorshipUtils.h"
#include "util/XDROperators.h"
#include "util/types.h"
#include <Tracy.hpp>

namespace stellar
{

AccountEntryExtensionV1&
prepareAccountEntryExtensionV1(AccountEntry& ae)
{
    if (ae.ext.v() == 0)
    {
        ae.ext.v(1);
        ae.ext.v1().liabilities = Liabilities{0, 0};
    }
    return ae.ext.v1();
}

AccountEntryExtensionV2&
prepareAccountEntryExtensionV2(AccountEntry& ae)
{
    auto& extV1 = prepareAccountEntryExtensionV1(ae);
    if (extV1.ext.v() == 0)
    {
        extV1.ext.v(2);
        auto& extV2 = extV1.ext.v2();
        extV2.signerSponsoringIDs.resize(
            static_cast<uint32_t>(ae.signers.size()));
    }
    return extV1.ext.v2();
}
AccountEntryExtensionV3&
prepareAccountEntryExtensionV3(AccountEntry& ae)
{
    auto& extV2 = prepareAccountEntryExtensionV2(ae);
    if (extV2.ext.v() == 0) {
        extV2.ext.v(3);
    }
    return extV2.ext.v3();
}

TrustLineEntry::_ext_t::_v1_t&
prepareTrustLineEntryExtensionV1(TrustLineEntry& tl)
{
    if (tl.ext.v() == 0)
    {
        tl.ext.v(1);
        tl.ext.v1().liabilities = Liabilities{0, 0};
    }
    return tl.ext.v1();
}

TrustLineEntryExtensionV2&
prepareTrustLineEntryExtensionV2(TrustLineEntry& tl)
{
    auto& extV1 = prepareTrustLineEntryExtensionV1(tl);

    if (extV1.ext.v() == 0)
    {
        extV1.ext.v(2);
        extV1.ext.v2().liquidityPoolUseCount = 0;
    }
    return extV1.ext.v2();
}

LedgerEntryExtensionV1&
prepareLedgerEntryExtensionV1(LedgerEntry& le)
{
    if (le.ext.v() == 0)
    {
        le.ext.v(1);
        le.ext.v1().sponsoringID.reset();
    }
    return le.ext.v1();
}

AccountEntryExtensionV2&
getAccountEntryExtensionV2(AccountEntry& ae)
{
    if (ae.ext.v() != 1 || ae.ext.v1().ext.v() != 2)
    {
        throw std::runtime_error("expected AccountEntry extension V2");
    }
    return ae.ext.v1().ext.v2();
}

AccountEntryExtensionV3&
getAccountEntryExtensionV3(AccountEntry& ae)
{
    if (ae.ext.v() != 1 || ae.ext.v1().ext.v() != 2 || ae.ext.v1().ext.v2().ext.v() != 3)
    {
        throw std::runtime_error("expected AccountEntry extension V3");
    }
    return ae.ext.v1().ext.v2().ext.v3();
}

AccountEntryExtensionV3 const&
getAccountEntryExtensionV3(AccountEntry const& ae)
{
    if (ae.ext.v() != 1 || ae.ext.v1().ext.v() != 2 || ae.ext.v1().ext.v2().ext.v() != 3)
    {
        throw std::runtime_error("expected AccountEntry extension V3");
    }
    return ae.ext.v1().ext.v2().ext.v3();
}

bool hasIssuedAssetLog(AccountEntry const& ae, AssetCode const& code)
{
    if (!hasAccountEntryExtV3(ae)) {
        return false;
    }
    auto const& v3 = getAccountEntryExtensionV3(ae);
    for (auto& issuedLog : v3.issuedAmounts) {
        if (issuedLog.code == code) {
            return true;
        }
    }
    return false;
}

IssuedAssetLog& getIssuedAssetLog(AccountEntry& ae, AssetCode const& code) {
    auto& v3 = getAccountEntryExtensionV3(ae);
    for (auto& issuedLog : v3.issuedAmounts) {
        if (issuedLog.code == code) {
            return issuedLog;
        }
    }
    throw std::runtime_error("issued asset code not found");
}

IssuedAssetLog const& getIssuedAssetLog(AccountEntry const& ae, AssetCode const& code) {
    auto const& v3 = getAccountEntryExtensionV3(ae);
    for (auto const& issuedLog : v3.issuedAmounts) {
        if (issuedLog.code == code) {
            return issuedLog;
        }
    }
    throw std::runtime_error("issued asset code not found");
}

static bool
IssuedAssetLogSorter(IssuedAssetLog const& l1, IssuedAssetLog const& l2) {
    return l1.code < l2.code;
}

void addNewIssuedAssetLog(AccountEntry& ae, AssetCode const& code) {
    if (hasIssuedAssetLog(ae, code)) {
        throw std::runtime_error("asset issuance log already exists");
    }

    if (!hasAccountEntryExtV3(ae)) {
        prepareAccountEntryExtensionV3(ae);
    }

    auto& v3 = getAccountEntryExtensionV3(ae);

    IssuedAssetLog newLog;
    newLog.code = code;
    newLog.issuedAmount = 0;
    v3.issuedAmounts.push_back(newLog);
    std::sort(v3.issuedAmounts.begin(), v3.issuedAmounts.end(), IssuedAssetLogSorter);
}

void trimIssuedAssetLog(AccountEntry& ae, AssetCode const& code) {
    auto& v3 = getAccountEntryExtensionV3(ae);
    for (auto iter = v3.issuedAmounts.begin(); iter != v3.issuedAmounts.end(); iter++) {
        if (iter -> code == code) {
            v3.issuedAmounts.erase(iter);
            return;
        }
    }
    throw std::runtime_error("can't delete nonexistent issuance log!");
}


TrustLineEntryExtensionV2&
getTrustLineEntryExtensionV2(TrustLineEntry& tl)
{
    if (!hasTrustLineEntryExtV2(tl))
    {
        throw std::runtime_error("expected TrustLineEntry extension V2");
    }

    return tl.ext.v1().ext.v2();
}

LedgerEntryExtensionV1&
getLedgerEntryExtensionV1(LedgerEntry& le)
{
    if (le.ext.v() != 1)
    {
        throw std::runtime_error("expected LedgerEntry extension V1");
    }

    return le.ext.v1();
}

static bool
checkAuthorization(LedgerHeader const& header, LedgerEntry const& entry)
{
    if (header.ledgerVersion < 10)
    {
        if (!isAuthorized(entry))
        {
            return false;
        }
    }
    else if (!isAuthorizedToMaintainLiabilities(entry))
    {
        throw std::runtime_error("Invalid authorization");
    }

    return true;
}

LedgerKey
accountKey(AccountID const& accountID)
{
    LedgerKey key(ACCOUNT);
    key.account().accountID = accountID;
    return key;
}

LedgerKey
trustlineKey(AccountID const& accountID, Asset const& asset)
{
    return trustlineKey(accountID, assetToTrustLineAsset(asset));
}

LedgerKey
trustlineKey(AccountID const& accountID, TrustLineAsset const& asset)
{
    LedgerKey key(TRUSTLINE);
    key.trustLine().accountID = accountID;
    key.trustLine().asset = asset;
    return key;
}

LedgerKey
offerKey(AccountID const& sellerID, uint64_t offerID)
{
    LedgerKey key(OFFER);
    key.offer().sellerID = sellerID;
    key.offer().offerID = offerID;
    return key;
}

LedgerKey
dataKey(AccountID const& accountID, std::string const& dataName)
{
    LedgerKey key(DATA);
    key.data().accountID = accountID;
    key.data().dataName = dataName;
    return key;
}

LedgerKey
claimableBalanceKey(ClaimableBalanceID const& balanceID)
{
    LedgerKey key(CLAIMABLE_BALANCE);
    key.claimableBalance().balanceID = balanceID;
    return key;
}

LedgerKey
liquidityPoolKey(PoolID const& poolID)
{
    LedgerKey key(LIQUIDITY_POOL);
    key.liquidityPool().liquidityPoolID = poolID;
    return key;
}

LedgerKey speedexConfigKey()
{
    LedgerKey key(SPEEDEX_CONFIG);
    return key;
}


LedgerKey
poolShareTrustLineKey(AccountID const& accountID, PoolID const& poolID)
{
    LedgerKey key(TRUSTLINE);
    key.trustLine().accountID = accountID;
    key.trustLine().asset.type(ASSET_TYPE_POOL_SHARE);
    key.trustLine().asset.liquidityPoolID() = poolID;
    return key;
}

InternalLedgerKey
sponsorshipKey(AccountID const& sponsoredID)
{
    InternalLedgerKey gkey(InternalLedgerEntryType::SPONSORSHIP);
    gkey.sponsorshipKey().sponsoredID = sponsoredID;
    return gkey;
}

InternalLedgerKey
sponsorshipCounterKey(AccountID const& sponsoringID)
{
    InternalLedgerKey gkey(InternalLedgerEntryType::SPONSORSHIP_COUNTER);
    gkey.sponsorshipCounterKey().sponsoringID = sponsoringID;
    return gkey;
}

LedgerTxnEntry
loadAccount(AbstractLedgerTxn& ltx, AccountID const& accountID)
{
    ZoneScoped;
    return ltx.load(accountKey(accountID));
}

ConstLedgerTxnEntry
loadAccountWithoutRecord(AbstractLedgerTxn& ltx, AccountID const& accountID)
{
    ZoneScoped;
    return ltx.loadWithoutRecord(accountKey(accountID));
}

LedgerTxnEntry
loadData(AbstractLedgerTxn& ltx, AccountID const& accountID,
         std::string const& dataName)
{
    ZoneScoped;
    return ltx.load(dataKey(accountID, dataName));
}

LedgerTxnEntry
loadOffer(AbstractLedgerTxn& ltx, AccountID const& sellerID, int64_t offerID)
{
    ZoneScoped;
    return ltx.load(offerKey(sellerID, offerID));
}

LedgerTxnEntry
loadClaimableBalance(AbstractLedgerTxn& ltx,
                     ClaimableBalanceID const& balanceID)
{
    return ltx.load(claimableBalanceKey(balanceID));
}

TrustLineWrapper
loadTrustLine(AbstractLedgerTxn& ltx, AccountID const& accountID,
              Asset const& asset)
{
    ZoneScoped;
    return TrustLineWrapper(ltx, accountID, asset);
}

ConstTrustLineWrapper
loadTrustLineWithoutRecord(AbstractLedgerTxn& ltx, AccountID const& accountID,
                           Asset const& asset)
{
    ZoneScoped;
    return ConstTrustLineWrapper(ltx, accountID, asset);
}

TrustLineWrapper
loadTrustLineIfNotNative(AbstractLedgerTxn& ltx, AccountID const& accountID,
                         Asset const& asset)
{
    ZoneScoped;
    if (asset.type() == ASSET_TYPE_NATIVE)
    {
        return {};
    }
    return TrustLineWrapper(ltx, accountID, asset);
}

ConstTrustLineWrapper
loadTrustLineWithoutRecordIfNotNative(AbstractLedgerTxn& ltx,
                                      AccountID const& accountID,
                                      Asset const& asset)
{
    ZoneScoped;
    if (asset.type() == ASSET_TYPE_NATIVE)
    {
        return {};
    }
    return ConstTrustLineWrapper(ltx, accountID, asset);
}

LedgerTxnEntry
loadSponsorship(AbstractLedgerTxn& ltx, AccountID const& sponsoredID)
{
    return ltx.load(sponsorshipKey(sponsoredID));
}

LedgerTxnEntry
loadSponsorshipCounter(AbstractLedgerTxn& ltx, AccountID const& sponsoringID)
{
    return ltx.load(sponsorshipCounterKey(sponsoringID));
}

LedgerTxnEntry
loadSpeedexConfig(AbstractLedgerTxn& ltx)
{
    return ltx.load(speedexConfigKey());
}

SpeedexConfigSnapshotFrame
loadSpeedexConfigSnapshot(AbstractLedgerTxn&ltx)
{
    return SpeedexConfigSnapshotFrame(ltx.loadSnapshotEntry(speedexConfigKey()));
}

LedgerTxnEntry
loadPoolShareTrustLine(AbstractLedgerTxn& ltx, AccountID const& accountID,
                       PoolID const& poolID)
{
    ZoneScoped;
    return ltx.load(poolShareTrustLineKey(accountID, poolID));
}

LedgerTxnEntry
loadLiquidityPool(AbstractLedgerTxn& ltx, PoolID const& poolID)
{
    ZoneScoped;
    return ltx.load(liquidityPoolKey(poolID));
}

static void
acquireOrReleaseLiabilities(AbstractLedgerTxn& ltx,
                            LedgerTxnHeader const& header,
                            LedgerTxnEntry const& offerEntry, bool isAcquire)
{
    ZoneScoped;
    // This should never happen
    auto const& offer = offerEntry.current().data.offer();
    if (offer.buying == offer.selling)
    {
        throw std::runtime_error("buying and selling same asset");
    }
    auto const& sellerID = offer.sellerID;

    auto loadAccountAndValidate = [&ltx, &sellerID]() {
        auto account = stellar::loadAccount(ltx, sellerID);
        if (!account)
        {
            throw std::runtime_error("account does not exist");
        }
        return account;
    };

    auto loadTrustAndValidate = [&ltx, &sellerID](Asset const& asset) {
        auto trust = stellar::loadTrustLine(ltx, sellerID, asset);
        if (!trust)
        {
            throw std::runtime_error("trustline does not exist");
        }
        return trust;
    };

    int64_t buyingLiabilities =
        isAcquire ? getOfferBuyingLiabilities(header, offerEntry)
                  : -getOfferBuyingLiabilities(header, offerEntry);
    if (offer.buying.type() == ASSET_TYPE_NATIVE)
    {
        auto account = loadAccountAndValidate();
        if (!addBuyingLiabilities(header, account, buyingLiabilities))
        {
            throw std::runtime_error("could not add buying liabilities");
        }
    }
    else
    {
        auto buyingTrust = loadTrustAndValidate(offer.buying);
        if (!buyingTrust.addBuyingLiabilities(header, buyingLiabilities))
        {
            throw std::runtime_error("could not add buying liabilities");
        }
    }

    int64_t sellingLiabilities =
        isAcquire ? getOfferSellingLiabilities(header, offerEntry)
                  : -getOfferSellingLiabilities(header, offerEntry);
    if (offer.selling.type() == ASSET_TYPE_NATIVE)
    {
        auto account = loadAccountAndValidate();
        if (!addSellingLiabilities(header, account, sellingLiabilities))
        {
            throw std::runtime_error("could not add selling liabilities");
        }
    }
    else
    {
        auto sellingTrust = loadTrustAndValidate(offer.selling);
        if (!sellingTrust.addSellingLiabilities(header, sellingLiabilities))
        {
            throw std::runtime_error("could not add selling liabilities");
        }
    }
}

void
acquireLiabilities(AbstractLedgerTxn& ltx, LedgerTxnHeader const& header,
                   LedgerTxnEntry const& offer)
{
    acquireOrReleaseLiabilities(ltx, header, offer, true);
}

bool
addBalanceSkipAuthorization(LedgerTxnHeader const& header,
                            LedgerTxnEntry& entry, int64_t amount)
{
    auto& tl = entry.current().data.trustLine();
    auto newBalance = tl.balance;
    if (!stellar::addBalance(newBalance, amount, tl.limit))
    {
        return false;
    }
    if (header.current().ledgerVersion >= 10)
    {
        if (newBalance < getSellingLiabilities(header, entry))
        {
            return false;
        }
        if (newBalance > tl.limit - getBuyingLiabilities(header, entry))
        {
            return false;
        }
    }

    tl.balance = newBalance;
    return true;
}

bool
addBalance(LedgerTxnHeader const& header, LedgerTxnEntry& entry, int64_t delta)
{
    if (entry.current().data.type() == ACCOUNT)
    {
        if (delta == 0)
        {
            return true;
        }

        auto& acc = entry.current().data.account();
        auto newBalance = acc.balance;
        if (!stellar::addBalance(newBalance, delta))
        {
            return false;
        }
        if (header.current().ledgerVersion >= 10)
        {
            auto minBalance = getMinBalance(header.current(), acc);
            if (delta < 0 &&
                newBalance - minBalance < getSellingLiabilities(header, entry))
            {
                return false;
            }
            if (newBalance > INT64_MAX - getBuyingLiabilities(header, entry))
            {
                return false;
            }
        }

        acc.balance = newBalance;
        return true;
    }
    else if (entry.current().data.type() == TRUSTLINE)
    {
        if (delta == 0)
        {
            return true;
        }

        if (!checkAuthorization(header.current(), entry.current()))
        {
            return false;
        }

        return addBalanceSkipAuthorization(header, entry, delta);
    }
    else
    {
        throw std::runtime_error("Unknown LedgerEntry type");
    }
}

bool issueAsset(LedgerTxnEntry& entry, AssetCode const& code, int64_t delta)
{
    if (entry.current().data.type() == ACCOUNT)
    {
        if (isIssuanceLimitedAccount(entry)) {
            auto& ae = entry.current().data.account();
            if (!hasAccountEntryExtV3(ae)) {
                prepareAccountEntryExtensionV3(ae);
            }
            if (!hasIssuedAssetLog(ae, code)) {
                addNewIssuedAssetLog(ae, code);
            }
            auto& log = getIssuedAssetLog(ae, code);

            if (log.issuedAmount < 0) {
                throw std::logic_error("started with a negative amount?!?");
            }

            if (INT64_MAX - log.issuedAmount < delta) {
                return false;
            }
       
            if (delta < 0 && log.issuedAmount + delta < 0)
            {
                // somehow debiting an account by more than was ever issued
                return false;
            }
            log.issuedAmount += delta;
            if (log.issuedAmount == 0) {
                trimIssuedAssetLog(ae, code);
            }
            if (log.issuedAmount < 0) {
                throw std::logic_error("somehow issued a negative amount of an asset!");
            }
            return true;
        }
        return true;
    }
    throw std::logic_error("can't issue on non Account entry");
}

bool
addBuyingLiabilities(LedgerTxnHeader const& header, LedgerTxnEntry& entry,
                     int64_t delta)
{
    int64_t buyingLiab = getBuyingLiabilities(header, entry);

    // Fast-succeed when not actually adding any liabilities
    if (delta == 0)
    {
        return true;
    }

    if (entry.current().data.type() == ACCOUNT)
    {
        auto& acc = entry.current().data.account();

        int64_t maxLiabilities = INT64_MAX - acc.balance;
        bool res = stellar::addBalance(buyingLiab, delta, maxLiabilities);
        if (res)
        {
            prepareAccountEntryExtensionV1(acc).liabilities.buying = buyingLiab;
        }
        return res;
    }
    else if (entry.current().data.type() == TRUSTLINE)
    {
        if (!checkAuthorization(header.current(), entry.current()))
        {
            return false;
        }

        auto& tl = entry.current().data.trustLine();
        int64_t maxLiabilities = tl.limit - tl.balance;
        bool res = stellar::addBalance(buyingLiab, delta, maxLiabilities);
        if (res)
        {
            prepareTrustLineEntryExtensionV1(tl).liabilities.buying =
                buyingLiab;
        }
        return res;
    }
    else
    {
        throw std::runtime_error("Unknown LedgerEntry type");
    }
}

bool
addSellingLiabilities(LedgerTxnHeader const& header, LedgerTxnEntry& entry,
                      int64_t delta)
{
    int64_t sellingLiab = getSellingLiabilities(header, entry);

    // Fast-succeed when not actually adding any liabilities
    if (delta == 0)
    {
        return true;
    }

    if (entry.current().data.type() == ACCOUNT)
    {
        auto& acc = entry.current().data.account();
        int64_t maxLiabilities =
            acc.balance - getMinBalance(header.current(), acc);
        if (maxLiabilities < 0)
        {
            return false;
        }

        bool res = stellar::addBalance(sellingLiab, delta, maxLiabilities);
        if (res)
        {
            prepareAccountEntryExtensionV1(acc).liabilities.selling =
                sellingLiab;
        }
        return res;
    }
    else if (entry.current().data.type() == TRUSTLINE)
    {
        if (!checkAuthorization(header.current(), entry.current()))
        {
            return false;
        }

        auto& tl = entry.current().data.trustLine();
        int64_t maxLiabilities = tl.balance;
        bool res = stellar::addBalance(sellingLiab, delta, maxLiabilities);
        if (res)
        {
            prepareTrustLineEntryExtensionV1(tl).liabilities.selling =
                sellingLiab;
        }
        return res;
    }
    else
    {
        throw std::runtime_error("Unknown LedgerEntry type");
    }
}

uint64_t
generateID(LedgerTxnHeader& header)
{
    return ++header.current().idPool;
}

int64_t
getAvailableBalance(LedgerHeader const& header, LedgerEntry const& le)
{
    int64_t avail = 0;
    if (le.data.type() == ACCOUNT)
    {
        auto const& acc = le.data.account();
        avail = acc.balance - getMinBalance(header, acc);
    }
    else if (le.data.type() == TRUSTLINE)
    {
        // We only want to check auth starting from V10, so no need to look at
        // the return value. This will throw if unauthorized
        checkAuthorization(header, le);

        avail = le.data.trustLine().balance;
    }
    else
    {
        throw std::runtime_error("Unknown LedgerEntry type");
    }

    if (header.ledgerVersion >= 10)
    {
        avail -= getSellingLiabilities(header, le);
    }
    return avail;
}

int64_t
getAvailableBalance(LedgerTxnHeader const& header, LedgerTxnEntry const& entry)
{
    return getAvailableBalance(header.current(), entry.current());
}

int64_t
getAvailableBalance(LedgerTxnHeader const& header,
                    ConstLedgerTxnEntry const& entry)
{
    return getAvailableBalance(header.current(), entry.current());
}

int64_t 
getAvailableBalance(LedgerTxnHeader const& header, AbstractLedgerTxn& ltx, AccountID account, Asset asset) {
    if (asset.type() == ASSET_TYPE_NATIVE) {
        auto accountEntry = loadAccount(ltx, account);
        return getAvailableBalance(header.current(), accountEntry.current());
    } else {
        auto tl = loadTrustLine(ltx, account, asset);
        return tl.getAvailableBalance(header);
    }
}


int64_t
getBuyingLiabilities(LedgerTxnHeader const& header, LedgerEntry const& le)
{
    if (header.current().ledgerVersion < 10)
    {
        throw std::runtime_error("Liabilities accessed before version 10");
    }

    if (le.data.type() == ACCOUNT)
    {
        auto const& acc = le.data.account();
        return (acc.ext.v() == 0) ? 0 : acc.ext.v1().liabilities.buying;
    }
    else if (le.data.type() == TRUSTLINE)
    {
        auto const& tl = le.data.trustLine();
        return (tl.ext.v() == 0) ? 0 : tl.ext.v1().liabilities.buying;
    }
    throw std::runtime_error("Unknown LedgerEntry type");
}

int64_t
getBuyingLiabilities(LedgerTxnHeader const& header, LedgerTxnEntry const& entry)
{
    return getBuyingLiabilities(header, entry.current());
}

int64_t
getMaxAmountReceive(LedgerTxnHeader const& header, LedgerEntry const& le)
{
    if (le.data.type() == ACCOUNT)
    {
        int64_t maxReceive = INT64_MAX;
        if (header.current().ledgerVersion >= 10)
        {
            auto const& acc = le.data.account();
            maxReceive -= acc.balance + getBuyingLiabilities(header, le);
        }
        return maxReceive;
    }
    if (le.data.type() == TRUSTLINE)
    {
        if (!checkAuthorization(header.current(), le))
        {
            return 0;
        }

        auto const& tl = le.data.trustLine();
        int64_t amount = tl.limit - tl.balance;
        if (header.current().ledgerVersion >= 10)
        {
            amount -= getBuyingLiabilities(header, le);
        }
        return amount;
    }
    else
    {
        throw std::runtime_error("Unknown LedgerEntry type");
    }
}

int64_t
getMaxAmountReceive(LedgerTxnHeader const& header, LedgerTxnEntry const& entry)
{
    return getMaxAmountReceive(header, entry.current());
}

int64_t
getMaxAmountReceive(LedgerTxnHeader const& header,
                    ConstLedgerTxnEntry const& entry)
{
    return getMaxAmountReceive(header, entry.current());
}

int64_t
getMinBalance(LedgerHeader const& header, AccountEntry const& acc)
{
    uint32_t numSponsoring = 0;
    uint32_t numSponsored = 0;
    if (header.ledgerVersion >= 14 && hasAccountEntryExtV2(acc))
    {
        numSponsoring = acc.ext.v1().ext.v2().numSponsoring;
        numSponsored = acc.ext.v1().ext.v2().numSponsored;
    }
    return getMinBalance(header, acc.numSubEntries, numSponsoring,
                         numSponsored);
}

int64_t
getMinBalance(LedgerHeader const& lh, uint32_t numSubentries,
              uint32_t numSponsoring, uint32_t numSponsored)
{
    if (lh.ledgerVersion < 14 && (numSponsored != 0 || numSponsoring != 0))
    {
        throw std::runtime_error("unexpected sponsorship state");
    }

    if (lh.ledgerVersion <= 8)
    {
        return (2 + numSubentries) * lh.baseReserve;
    }
    else
    {
        int64_t effEntries = 2LL;
        effEntries += numSubentries;
        effEntries += numSponsoring;
        effEntries -= numSponsored;
        if (effEntries < 0)
        {
            throw std::runtime_error("unexpected account state");
        }
        return effEntries * int64_t(lh.baseReserve);
    }
}

int64_t
getMinimumLimit(LedgerTxnHeader const& header, LedgerEntry const& le)
{
    auto const& tl = le.data.trustLine();
    int64_t minLimit = tl.balance;
    if (header.current().ledgerVersion >= 10)
    {
        minLimit += getBuyingLiabilities(header, le);
    }
    return minLimit;
}

int64_t
getMinimumLimit(LedgerTxnHeader const& header, LedgerTxnEntry const& entry)
{
    return getMinimumLimit(header, entry.current());
}

int64_t
getMinimumLimit(LedgerTxnHeader const& header, ConstLedgerTxnEntry const& entry)
{
    return getMinimumLimit(header, entry.current());
}

int64_t
getOfferBuyingLiabilities(LedgerTxnHeader const& header,
                          LedgerEntry const& entry)
{
    if (header.current().ledgerVersion < 10)
    {
        throw std::runtime_error(
            "Offer liabilities calculated before version 10");
    }
    auto const& oe = entry.data.offer();
    auto res = exchangeV10WithoutPriceErrorThresholds(
        oe.price, oe.amount, INT64_MAX, INT64_MAX, INT64_MAX,
        RoundingType::NORMAL);
    return res.numSheepSend;
}

int64_t
getOfferBuyingLiabilities(LedgerTxnHeader const& header,
                          LedgerTxnEntry const& entry)
{
    return getOfferBuyingLiabilities(header, entry.current());
}

int64_t
getOfferSellingLiabilities(LedgerTxnHeader const& header,
                           LedgerEntry const& entry)
{
    if (header.current().ledgerVersion < 10)
    {
        throw std::runtime_error(
            "Offer liabilities calculated before version 10");
    }
    auto const& oe = entry.data.offer();
    auto res = exchangeV10WithoutPriceErrorThresholds(
        oe.price, oe.amount, INT64_MAX, INT64_MAX, INT64_MAX,
        RoundingType::NORMAL);
    return res.numWheatReceived;
}

int64_t
getOfferSellingLiabilities(LedgerTxnHeader const& header,
                           LedgerTxnEntry const& entry)
{
    return getOfferSellingLiabilities(header, entry.current());
}

int64_t
getSellingLiabilities(LedgerHeader const& header, LedgerEntry const& le)
{
    if (header.ledgerVersion < 10)
    {
        throw std::runtime_error("Liabilities accessed before version 10");
    }

    if (le.data.type() == ACCOUNT)
    {
        auto const& acc = le.data.account();
        return (acc.ext.v() == 0) ? 0 : acc.ext.v1().liabilities.selling;
    }
    else if (le.data.type() == TRUSTLINE)
    {
        auto const& tl = le.data.trustLine();
        return (tl.ext.v() == 0) ? 0 : tl.ext.v1().liabilities.selling;
    }
    throw std::runtime_error("Unknown LedgerEntry type");
}

int64_t
getSellingLiabilities(LedgerTxnHeader const& header,
                      LedgerTxnEntry const& entry)
{
    return getSellingLiabilities(header.current(), entry.current());
}

SequenceNumber
getStartingSequenceNumber(uint32_t ledgerSeq)
{
    if (ledgerSeq > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
    {
        throw std::runtime_error("overflowed getStartingSequenceNumber");
    }
    return static_cast<SequenceNumber>(ledgerSeq) << 32;
}

SequenceNumber
getStartingSequenceNumber(LedgerTxnHeader const& header)
{
    return getStartingSequenceNumber(header.current().ledgerSeq);
}

bool
isAuthorized(LedgerEntry const& le)
{
    return (le.data.trustLine().flags & AUTHORIZED_FLAG) != 0;
}

bool
isAuthorized(LedgerTxnEntry const& entry)
{
    return isAuthorized(entry.current());
}

bool
isAuthorized(ConstLedgerTxnEntry const& entry)
{
    return isAuthorized(entry.current());
}

bool
isAuthorizedToMaintainLiabilitiesUnsafe(uint32_t flags)
{
    return (flags & TRUSTLINE_AUTH_FLAGS) != 0;
}

bool
isIssuanceLimitedAccount(uint32_t flags)
{
    return (flags & AUTH_ISSUANCE_LIMIT) != 0;
}

bool
isIssuanceLimitedAccount(LedgerEntry const& entry)
{
    return isIssuanceLimitedAccount(entry.data.account().flags);
}

bool
isIssuanceLimitedAccount(LedgerTxnEntry const& entry)
{
    return isIssuanceLimitedAccount(entry.current());
}

bool isCommutativeTxEnabledAsset(AbstractLedgerTxn& ltx, Asset const& asset) {
    if (asset.type() == ASSET_TYPE_NATIVE) {
        return true;
    }
    auto issuerID = getIssuer(asset);
    auto acct = loadAccount(ltx, issuerID);
    if (!acct) return false;
    return isIssuanceLimitedAccount(acct.current());
}

bool isCommutativeTxEnabledAsset(AbstractLedgerTxn& ltx, TrustLineAsset const& tlAsset) {
    auto asset = trustLineAssetToAsset(tlAsset);
    // returns nullopt when asset is a pool share.
    // TODO to make pool shares tradable on speedex, edit this (and also change
    // speedexconfig to allow pool share assets, not just regular Asset types)
    if (!asset) {
        return false;
    }
    return isCommutativeTxEnabledAsset(ltx, *asset);
}

bool
isCommutativeTxEnabledTrustLine(LedgerEntry const& le)
{
    return isAuthorizedToMaintainLiabilities(le)
        && le.data.trustLine().limit == INT64_MAX;
}

bool
isCommutativeTxEnabledTrustLine(LedgerTxnEntry const& entry)
{
    return isCommutativeTxEnabledTrustLine(entry.current());
}

bool
isAuthorizedToMaintainLiabilities(LedgerEntry const& le)
{
    if (le.data.trustLine().asset.type() == ASSET_TYPE_POOL_SHARE)
    {
        return true;
    }
    return isAuthorizedToMaintainLiabilitiesUnsafe(le.data.trustLine().flags);
}

bool
isAuthorizedToMaintainLiabilities(LedgerTxnEntry const& entry)
{
    return isAuthorizedToMaintainLiabilities(entry.current());
}

bool
isAuthorizedToMaintainLiabilities(ConstLedgerTxnEntry const& entry)
{
    return isAuthorizedToMaintainLiabilities(entry.current());
}

bool
isAuthRequired(ConstLedgerTxnEntry const& entry)
{
    return (entry.current().data.account().flags & AUTH_REQUIRED_FLAG) != 0;
}

bool
isClawbackEnabledOnTrustline(TrustLineEntry const& tl)
{
    return (tl.flags & TRUSTLINE_CLAWBACK_ENABLED_FLAG) != 0;
}

bool
isClawbackEnabledOnTrustline(LedgerTxnEntry const& entry)
{
    return isClawbackEnabledOnTrustline(entry.current().data.trustLine());
}

bool
isClawbackEnabledOnClaimableBalance(ClaimableBalanceEntry const& entry)
{
    return entry.ext.v() == 1 && (entry.ext.v1().flags &
                                  CLAIMABLE_BALANCE_CLAWBACK_ENABLED_FLAG) != 0;
}

bool
isClawbackEnabledOnClaimableBalance(LedgerEntry const& entry)
{
    return isClawbackEnabledOnClaimableBalance(entry.data.claimableBalance());
}

bool
isClawbackEnabledOnAccount(LedgerEntry const& entry)
{
    return (entry.data.account().flags & AUTH_CLAWBACK_ENABLED_FLAG) != 0;
}

bool
isClawbackEnabledOnAccount(LedgerTxnEntry const& entry)
{
    return isClawbackEnabledOnAccount(entry.current());
}

bool
isClawbackEnabledOnAccount(ConstLedgerTxnEntry const& entry)
{
    return isClawbackEnabledOnAccount(entry.current());
}

bool
isImmutableAuth(LedgerEntry const& entry)
{
    return (entry.data.account().flags & AUTH_IMMUTABLE_FLAG) != 0;
}

bool
isImmutableAuth(LedgerTxnEntry const& entry)
{
    return isImmutableAuth(entry.current());
}



int64_t 
getRemainingAssetIssuance(LedgerEntry const& entry, AssetCode const& code)
{
    if (entry.data.type() == ACCOUNT)
    {
        auto issuedAmount = getIssuedAssetAmount(entry, code);
        if (issuedAmount) {
            return INT64_MAX - *issuedAmount;
        }
        return INT64_MAX;
    }
    throw std::logic_error("invalid asset issuance limit request");
}

int64_t
getRemainingAssetIssuance(LedgerTxnEntry const& entry, AssetCode const& code)
{
    return getRemainingAssetIssuance(entry.current(), code);
}

std::optional<int64_t> 
getIssuedAssetAmount(LedgerEntry const& entry, AssetCode const& code) {
    if (entry.data.type() == ACCOUNT) {
        if (!isIssuanceLimitedAccount(entry)) {
            return std::nullopt;
        }
        auto& ae = entry.data.account();
        if (!hasIssuedAssetLog(ae, code)) {
            return 0;
        }
        auto const& issuedLog = getIssuedAssetLog(ae, code);
        return issuedLog.issuedAmount;
    }
    throw std::logic_error("invalid assue issue amount request");
}

std::optional<int64_t> 
getIssuedAssetAmount(LedgerTxnEntry const& entry, AssetCode const& code) {
    return getIssuedAssetAmount(entry.current(), code);
}

void
releaseLiabilities(AbstractLedgerTxn& ltx, LedgerTxnHeader const& header,
                   LedgerTxnEntry const& offer)
{
    acquireOrReleaseLiabilities(ltx, header, offer, false);
}

bool
trustLineFlagIsValid(uint32_t flag, uint32_t ledgerVersion)
{
    return trustLineFlagMaskCheckIsValid(flag, ledgerVersion) &&
           (ledgerVersion < 13 || trustLineFlagAuthIsValid(flag));
}

bool
trustLineFlagAuthIsValid(uint32_t flag)
{
    static_assert(TRUSTLINE_AUTH_FLAGS == 3,
                  "condition only works for two flags");
    // multiple auth flags can't be set
    if ((flag & TRUSTLINE_AUTH_FLAGS) == TRUSTLINE_AUTH_FLAGS)
    {
        return false;
    }

    return true;
}

bool
trustLineFlagMaskCheckIsValid(uint32_t flag, uint32_t ledgerVersion)
{
    if (ledgerVersion < 13)
    {
        return (flag & ~MASK_TRUSTLINE_FLAGS) == 0;
    }
    else if (ledgerVersion < 17)
    {
        return (flag & ~MASK_TRUSTLINE_FLAGS_V13) == 0;
    }
    else
    {
        return (flag & ~MASK_TRUSTLINE_FLAGS_V17) == 0;
    }
}

bool
accountFlagIsValid(uint32_t flag, uint32_t ledgerVersion)
{
    return accountFlagMaskCheckIsValid(flag, ledgerVersion) &&
           accountFlagClawbackIsValid(flag, ledgerVersion);
}

bool
accountFlagClawbackIsValid(uint32_t flag, uint32_t ledgerVersion)
{
    if (ledgerVersion >= 17 && (flag & AUTH_CLAWBACK_ENABLED_FLAG) &&
        ((flag & AUTH_REVOCABLE_FLAG) == 0))
    {
        return false;
    }

    return true;
}

bool
accountFlagMaskCheckIsValid(uint32_t flag, uint32_t ledgerVersion)
{
    if (ledgerVersion < 17)
    {
        return (flag & ~MASK_ACCOUNT_FLAGS) == 0;
    }

    return (flag & ~MASK_ACCOUNT_FLAGS_V17) == 0;
}

AccountID
toAccountID(MuxedAccount const& m)
{
    AccountID ret(static_cast<PublicKeyType>(m.type() & 0xff));
    switch (m.type())
    {
    case KEY_TYPE_ED25519:
        ret.ed25519() = m.ed25519();
        break;
    case KEY_TYPE_MUXED_ED25519:
        ret.ed25519() = m.med25519().ed25519;
        break;
    default:
        // this would be a bug
        abort();
    }
    return ret;
}

MuxedAccount
toMuxedAccount(AccountID const& a)
{
    MuxedAccount ret(static_cast<CryptoKeyType>(a.type()));
    switch (a.type())
    {
    case PUBLIC_KEY_TYPE_ED25519:
        ret.ed25519() = a.ed25519();
        break;
    default:
        // this would be a bug
        abort();
    }
    return ret;
}

bool
trustLineFlagIsValid(uint32_t flag, LedgerTxnHeader const& header)
{
    return trustLineFlagIsValid(flag, header.current().ledgerVersion);
}

uint64_t
getUpperBoundCloseTimeOffset(Application& app, uint64_t lastCloseTime)
{
    uint64_t currentTime = VirtualClock::to_time_t(app.getClock().system_now());

    // account for the time between closeTime and now
    uint64_t closeTimeDrift =
        currentTime <= lastCloseTime ? 0 : currentTime - lastCloseTime;

    return app.getConfig().getExpectedLedgerCloseTime().count() *
               EXPECTED_CLOSE_TIME_MULT +
           closeTimeDrift;
}

bool
hasAccountEntryExtV2(AccountEntry const& ae)
{
    return ae.ext.v() == 1 && ae.ext.v1().ext.v() == 2;
}

bool 
hasAccountEntryExtV3(AccountEntry const& ae)
{
    return hasAccountEntryExtV2(ae) && ae.ext.v1().ext.v2().ext.v() == 3;
}

bool
hasTrustLineEntryExtV2(TrustLineEntry const& tl)
{
    return tl.ext.v() == 1 && tl.ext.v1().ext.v() == 2;
}

Asset
getAsset(AccountID const& issuer, AssetCode const& assetCode)
{
    Asset asset;
    asset.type(assetCode.type());
    if (assetCode.type() == ASSET_TYPE_CREDIT_ALPHANUM4)
    {
        asset.alphaNum4().assetCode = assetCode.assetCode4();
        asset.alphaNum4().issuer = issuer;
    }
    else if (assetCode.type() == ASSET_TYPE_CREDIT_ALPHANUM12)
    {
        asset.alphaNum12().assetCode = assetCode.assetCode12();
        asset.alphaNum12().issuer = issuer;
    }
    else
    {
        throw std::runtime_error("Unexpected assetCode type");
    }

    return asset;
}

Asset getNativeAsset()
{
    Asset asset;
    asset.type(ASSET_TYPE_NATIVE);
    return asset;
}

AssetCode getAssetCode(Asset const& asset)
{
    AssetCode out;
    if (asset.type() == ASSET_TYPE_CREDIT_ALPHANUM4) {
        out.type(ASSET_TYPE_CREDIT_ALPHANUM4);
        out.assetCode4() = asset.alphaNum4().assetCode;
        return out;
    }
    if (asset.type() == ASSET_TYPE_CREDIT_ALPHANUM12) {
        out.type(ASSET_TYPE_CREDIT_ALPHANUM12);
        out.assetCode12() = asset.alphaNum12().assetCode;
        return out;
    }
    throw std::runtime_error("invalid asset type for making asset code");
}

/*
AccountID getIssuer(Asset const& asset) {
    if (asset.type() == ASSET_TYPE_CREDIT_ALPHANUM4) {
        return asset.alphaNum4().issuer;
    }
    if (asset.type() == ASSET_TYPE_CREDIT_ALPHANUM12) {
        return asset.alphaNum12().issuer;
    }
    throw std::runtime_error("unexpected asset type");
} */


bool
claimableBalanceFlagIsValid(ClaimableBalanceEntry const& cb)
{
    if (cb.ext.v() == 1)
    {
        return cb.ext.v1().flags == MASK_CLAIMABLE_BALANCE_FLAGS;
    }

    return true;
}

void
removeOffersByAccountAndAsset(AbstractLedgerTxn& ltx, AccountID const& account,
                              Asset const& asset)
{
    LedgerTxn ltxInner(ltx);

    auto header = ltxInner.loadHeader();
    auto offers = ltxInner.loadOffersByAccountAndAsset(account, asset);
    for (auto& offer : offers)
    {
        auto const& oe = offer.current().data.offer();
        if (!(oe.sellerID == account))
        {
            throw std::runtime_error("Offer not owned by expected account");
        }
        else if (!(oe.buying == asset || oe.selling == asset))
        {
            throw std::runtime_error(
                "Offer not buying or selling expected asset");
        }

        releaseLiabilities(ltxInner, header, offer);
        auto trustAcc = stellar::loadAccount(ltxInner, account);
        removeEntryWithPossibleSponsorship(ltxInner, header, offer.current(),
                                           trustAcc);
        offer.erase();
    }
    ltxInner.commit();
}

template <typename T>
T
assetConversionHelper(Asset const& asset)
{
    T otherAsset;
    otherAsset.type(asset.type());

    switch (asset.type())
    {
    case stellar::ASSET_TYPE_NATIVE:
        break;
    case stellar::ASSET_TYPE_CREDIT_ALPHANUM4:
        otherAsset.alphaNum4() = asset.alphaNum4();
        break;
    case stellar::ASSET_TYPE_CREDIT_ALPHANUM12:
        otherAsset.alphaNum12() = asset.alphaNum12();
        break;
    case stellar::ASSET_TYPE_POOL_SHARE:
        throw std::runtime_error("Asset can't have type ASSET_TYPE_POOL_SHARE");
    default:
        throw std::runtime_error("Unknown asset type");
    }

    return otherAsset;
}

TrustLineAsset
assetToTrustLineAsset(Asset const& asset)
{
    return assetConversionHelper<TrustLineAsset>(asset);
}

ChangeTrustAsset
assetToChangeTrustAsset(Asset const& asset)
{
    return assetConversionHelper<ChangeTrustAsset>(asset);
}

TrustLineAsset
changeTrustAssetToTrustLineAsset(ChangeTrustAsset const& ctAsset)
{
    TrustLineAsset tlAsset;
    tlAsset.type(ctAsset.type());

    switch (ctAsset.type())
    {
    case stellar::ASSET_TYPE_NATIVE:
        break;
    case stellar::ASSET_TYPE_CREDIT_ALPHANUM4:
        tlAsset.alphaNum4() = ctAsset.alphaNum4();
        break;
    case stellar::ASSET_TYPE_CREDIT_ALPHANUM12:
        tlAsset.alphaNum12() = ctAsset.alphaNum12();
        break;
    case stellar::ASSET_TYPE_POOL_SHARE:
        tlAsset.liquidityPoolID() = xdrSha256(ctAsset.liquidityPool());
        break;
    default:
        throw std::runtime_error("Unknown asset type");
    }

    return tlAsset;
}

std::optional<Asset>
trustLineAssetToAsset(TrustLineAsset const& tlAsset) {
    Asset asset;
    switch(tlAsset.type()) {
        case stellar::ASSET_TYPE_NATIVE:
            asset.type(tlAsset.type());
            break;
        case stellar::ASSET_TYPE_CREDIT_ALPHANUM4:
            asset.type(tlAsset.type());
            asset.alphaNum4() = tlAsset.alphaNum4();
            break;
        case stellar::ASSET_TYPE_CREDIT_ALPHANUM12:
            asset.type(tlAsset.type());    
            asset.alphaNum12() = tlAsset.alphaNum12();
            break;
        case stellar::ASSET_TYPE_POOL_SHARE:
            return std::nullopt;
            break;
        default:
            throw std::runtime_error("unknown asset type");
    }
    return asset;
}

int64_t
getPoolWithdrawalAmount(int64_t amountPoolShares, int64_t totalPoolShares,
                        int64_t reserve)
{
    if (amountPoolShares > totalPoolShares)
    {
        throw std::runtime_error("Invalid amountPoolShares");
    }

    return bigDivide(amountPoolShares, reserve, totalPoolShares, ROUND_DOWN);
}

namespace detail
{
struct MuxChecker
{
    bool mHasMuxedAccount{false};

    void
    operator()(stellar::MuxedAccount const& t)
    {
        // checks if this is a multiplexed account,
        // such as KEY_TYPE_MUXED_ED25519
        if ((t.type() & 0x100) != 0)
        {
            mHasMuxedAccount = true;
        }
    }

    template <typename T>
    std::enable_if_t<(xdr::xdr_traits<T>::is_container ||
                      xdr::xdr_traits<T>::is_class)>
    operator()(T const& t)
    {
        if (!mHasMuxedAccount)
        {
            xdr::xdr_traits<T>::save(*this, t);
        }
    }

    template <typename T>
    std::enable_if_t<!(xdr::xdr_traits<T>::is_container ||
                       xdr::xdr_traits<T>::is_class)>
    operator()(T const& t)
    {
    }
};
} // namespace detail

bool
hasMuxedAccount(TransactionEnvelope const& e)
{
    detail::MuxChecker c;
    c(e);
    return c.mHasMuxedAccount;
}

ClaimAtom
makeClaimAtom(uint32_t ledgerVersion, AccountID const& accountID,
              int64_t offerID, Asset const& wheat, int64_t numWheatReceived,
              Asset const& sheep, int64_t numSheepSend)
{
    ClaimAtom atom;
    if (ledgerVersion <= 17)
    {
        atom.type(CLAIM_ATOM_TYPE_V0);
        atom.v0() = ClaimOfferAtomV0(accountID.ed25519(), offerID, wheat,
                                     numWheatReceived, sheep, numSheepSend);
    }
    else
    {
        atom.type(CLAIM_ATOM_TYPE_ORDER_BOOK);
        atom.orderBook() = ClaimOfferAtom(
            accountID, offerID, wheat, numWheatReceived, sheep, numSheepSend);
    }
    return atom;
}


PoolID getPoolID(Asset const& selling, Asset const& buying) {
    LiquidityPoolParameters params;
    params.type(LIQUIDITY_POOL_CONSTANT_PRODUCT);
    if (selling < buying) {
        params.constantProduct().assetA = selling;
        params.constantProduct().assetB = buying;
    } else {
        params.constantProduct().assetA = buying;
        params.constantProduct().assetB = selling;
    }
    params.constantProduct().fee = LIQUIDITY_POOL_FEE_V18;

    return xdrSha256(params);
}

} // namespace stellar
