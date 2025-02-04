#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace stellar
{
class AbstractLedgerTxn;
class TransactionCommutativityRequirements;

class PaymentOpFrame : public OperationFrame
{
    PaymentResult&
    innerResult()
    {
        return mResult.tr().paymentResult();
    }
    PaymentOp const& mPayment;

  public:
    PaymentOpFrame(Operation const& op, OperationResult& res,
                   TransactionFrame& parentTx);

    bool doApply(AbstractLedgerTxn& ltx) override;
    bool doCheckValid(uint32_t ledgerVersion) override;

    bool doAddCommutativityRequirements(AbstractLedgerTxn& ltx, TransactionCommutativityRequirements& reqs) override;
    void doAddCommutativityRequirementsUnconditional(TransactionCommutativityRequirements& reqs) const override;

    void
    insertLedgerKeysToPrefetch(UnorderedSet<LedgerKey>& keys) const override;

    static PaymentResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().paymentResult().code();
    }
};
}
