#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/PeerSendQueue.h>

#include <xrpl/beast/unit_test/suite.h>

#include <xrpl.pb.h>

#include <memory>

namespace xrpl::test {

class PeerSendQueue_test : public beast::unit_test::Suite
{
    static std::shared_ptr<Message>
    transaction()
    {
        protocol::TMTransaction tx;
        tx.set_rawtransaction("tx");
        tx.set_status(protocol::tsNEW);
        return std::make_shared<Message>(tx, protocol::mtTRANSACTION);
    }

    static std::shared_ptr<Message>
    proposal()
    {
        protocol::TMProposeSet p;
        p.set_proposeseq(1);
        p.set_currenttxhash("h");
        p.set_previousledger("p");
        p.set_closetime(1);
        p.set_nodepubkey("k");
        p.set_signature("s");
        return std::make_shared<Message>(p, protocol::mtPROPOSE_LEDGER);
    }

    static std::shared_ptr<Message>
    validation()
    {
        protocol::TMValidation v;
        v.set_validation("v");
        return std::make_shared<Message>(v, protocol::mtVALIDATION);
    }

    void
    testClassification()
    {
        testcase("proposals and validations are priority, the rest is bulk");
        BEAST_EXPECT(proposal()->isPriority());
        BEAST_EXPECT(validation()->isPriority());
        BEAST_EXPECT(!transaction()->isPriority());

        protocol::TMPing ping;
        ping.set_type(protocol::TMPing::ptPING);
        BEAST_EXPECT(!Message(ping, protocol::mtPING).isPriority());
    }

    void
    testPriorityFirst()
    {
        testcase("priority lane drains before bulk regardless of arrival order");
        PeerSendQueue q;
        BEAST_EXPECT(q.empty());

        auto const tx1 = transaction();
        auto const tx2 = transaction();
        auto const prop = proposal();
        q.push(tx1);
        q.push(tx2);
        q.push(prop);
        BEAST_EXPECT(q.bulkSize() == 2);
        BEAST_EXPECT(q.prioritySize() == 1);

        BEAST_EXPECT(q.pop() == prop);
        BEAST_EXPECT(q.pop() == tx1);
        BEAST_EXPECT(q.pop() == tx2);
        BEAST_EXPECT(q.empty());
    }

    void
    testInterleaving()
    {
        testcase("a priority message pushed mid-stream jumps the remaining bulk");
        PeerSendQueue q;
        auto const tx1 = transaction();
        auto const tx2 = transaction();
        auto const tx3 = transaction();
        q.push(tx1);
        q.push(tx2);
        q.push(tx3);
        BEAST_EXPECT(q.pop() == tx1);

        auto const val = validation();
        q.push(val);
        BEAST_EXPECT(q.pop() == val);
        BEAST_EXPECT(q.pop() == tx2);
        BEAST_EXPECT(q.pop() == tx3);
        BEAST_EXPECT(q.empty());
    }

    void
    testLaneOrder()
    {
        testcase("order is preserved within each lane");
        PeerSendQueue q;
        auto const p1 = proposal();
        auto const v1 = validation();
        auto const p2 = proposal();
        q.push(p1);
        q.push(transaction());
        q.push(v1);
        q.push(p2);
        BEAST_EXPECT(q.prioritySize() == 3);
        BEAST_EXPECT(q.bulkSize() == 1);
        BEAST_EXPECT(q.pop() == p1);
        BEAST_EXPECT(q.pop() == v1);
        BEAST_EXPECT(q.pop() == p2);
        BEAST_EXPECT(q.prioritySize() == 0);
        BEAST_EXPECT(q.bulkSize() == 1);
    }

public:
    void
    run() override
    {
        testClassification();
        testPriorityFirst();
        testInterleaving();
        testLaneOrder();
    }
};

BEAST_DEFINE_TESTSUITE(PeerSendQueue, overlay, xrpl);

}  // namespace xrpl::test
