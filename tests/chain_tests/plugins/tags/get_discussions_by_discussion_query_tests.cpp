#include "get_discussions_by_common.hpp"
#include <scorum/tags/tags_api_objects.hpp>
#include <scorum/common_api/config.hpp>
#include <boost/test/unit_test.hpp>

namespace tags_tests {

using namespace tags::api;

BOOST_FIXTURE_TEST_SUITE(get_discussions_by_trending_tests, get_discussions_by_common)

SCORUM_TEST_CASE(no_votes_should_return_nothing)
{
    actor(initdelegate).give_sp(alice, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A","B","C"]})";
    });

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = true;
    q.tags = { "B", "C" };
    std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

    BOOST_REQUIRE_EQUAL(discussions.size(), 0);
}

SCORUM_TEST_CASE(no_requested_tag_should_return_nothing)
{
    actor(initdelegate).give_sp(alice, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A","B","C"]})";
    });

    actor(alice).vote(p1.author(), p1.permlink());

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = true;
    q.tags = { "D" };
    std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

    BOOST_REQUIRE_EQUAL(discussions.size(), 0);
}

SCORUM_TEST_CASE(should_return_voted_tags_intersection)
{
    actor(initdelegate).give_sp(alice, 1e9);
    actor(initdelegate).give_sp(bob, 1e9);
    actor(initdelegate).give_sp(sam, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A","B","C"]})";
    });
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["C","D","E"]})";
    });
    auto p3 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl3";
        op.body = "body3";
        op.json_metadata = R"({"tags":["B","C","D","E"]})";
    });

    actor(sam).vote(p1.author(), p1.permlink());

    actor(sam).vote(p2.author(), p2.permlink());
    actor(bob).vote(p2.author(), p2.permlink());

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = true;
    q.tags = { "B", "C" };
    {
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 1);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p1.permlink());
    }

    actor(sam).vote(p3.author(), p3.permlink());
    actor(bob).vote(p3.author(), p3.permlink());
    actor(alice).vote(p3.author(), p3.permlink());

    {
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 2);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p3.permlink());
        BOOST_REQUIRE_EQUAL(discussions[1].permlink, p1.permlink());
    }
}

SCORUM_TEST_CASE(should_return_voted_tags_union)
{
    actor(initdelegate).give_sp(alice, 1e9);
    actor(initdelegate).give_sp(bob, 1e9);
    actor(initdelegate).give_sp(sam, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A","B","C"]})";
    });
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["C","D","E"]})";
    });
    // this post will be skipped (despite it has max trending) cuz it doesn't have neither "B" or "D" tag
    auto p3 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl3";
        op.body = "body3";
        op.json_metadata = R"({"tags":["C","E"]})";
    });

    actor(sam).vote(p1.author(), p1.permlink());

    actor(sam).vote(p2.author(), p2.permlink());
    actor(bob).vote(p2.author(), p2.permlink());

    actor(sam).vote(p3.author(), p3.permlink());
    actor(bob).vote(p3.author(), p3.permlink());
    actor(alice).vote(p3.author(), p3.permlink());

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = false;
    q.tags = { "A", "D" };

    std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

    BOOST_REQUIRE_EQUAL(discussions.size(), 2);
    BOOST_REQUIRE_EQUAL(discussions[0].permlink, p2.permlink());
    BOOST_REQUIRE_EQUAL(discussions[1].permlink, p1.permlink());
}

SCORUM_TEST_CASE(check_pagination)
{
    actor(initdelegate).give_sp(alice, 1e9);
    actor(initdelegate).give_sp(bob, 1e9);
    actor(initdelegate).give_sp(sam, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A","B","C"]})";
    });
    // this post will be skipped cuz it doesn't have "C" tag
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["D","E"]})";
    });
    auto p3 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl3";
        op.body = "body3";
        op.json_metadata = R"({"tags":["B","C","D","E"]})";
    });
    auto p4 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl4";
        op.body = "body4";
        op.json_metadata = R"({"tags":["C","B","E"]})";
    });

    actor(sam).vote(p1.author(), p1.permlink());

    actor(sam).vote(p2.author(), p2.permlink());
    actor(bob).vote(p2.author(), p2.permlink());

    actor(sam).vote(p3.author(), p3.permlink());
    actor(bob).vote(p3.author(), p3.permlink());
    actor(alice).vote(p3.author(), p3.permlink());

    actor(bob).vote(p4.author(), p4.permlink());
    actor(sam).vote(p4.author(), p4.permlink());

    discussion_query q;
    q.limit = 2;
    q.tags_logical_and = true;
    q.tags = { "C", "B" };
    {
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 2);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p3.permlink());
        BOOST_REQUIRE_EQUAL(discussions[1].permlink, p4.permlink());

        q.start_author = discussions[1].author;
        q.start_permlink = discussions[1].permlink;
    }

    {
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 2);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p4.permlink());
        BOOST_REQUIRE_EQUAL(discussions[1].permlink, p1.permlink());
    }
}

SCORUM_TEST_CASE(check_only_first_8_tags_are_analized)
{
    actor(initdelegate).give_sp(bob, 1e9);
    actor(initdelegate).give_sp(sam, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        // I-K are ignored (see TAGS_TO_ANALIZE_COUNT)
        op.json_metadata = R"({"tags":["A","B","C","D","E","F","G","H","I","J","K"]})";
    });
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["H","I"]})";
    });

    actor(sam).vote(p1.author(), p1.permlink());

    actor(sam).vote(p2.author(), p2.permlink());
    actor(bob).vote(p2.author(), p2.permlink());

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = true;
    q.tags = { "I" };
    {
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 1);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p2.permlink());
    }
    {
        q.tags = { "H" };
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 2);
    }
}

SCORUM_TEST_CASE(check_truncate_body)
{
    actor(initdelegate).give_sp(sam, 1e9);

    auto p1 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "1234567890";
        op.json_metadata = R"({"tags":["I"]})";
    });

    actor(sam).vote(p1.author(), p1.permlink());

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = true;
    q.tags = { "I" };
    q.truncate_body = 5;
    std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

    BOOST_REQUIRE_EQUAL(discussions.size(), 1);
    BOOST_REQUIRE_EQUAL(discussions[0].body.size(), q.truncate_body);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(get_discussions_by_created_tests, get_discussions_by_common)

SCORUM_TEST_CASE(no_votes_should_return_union)
{
    actor(initdelegate).give_sp(alice, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A"]})";
    });
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["B"]})";
    });

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = false;
    q.tags = { "A", "B", "C" };
    std::vector<discussion> discussions = _api.get_discussions_by_created(q);

    BOOST_REQUIRE_EQUAL(discussions.size(), 2);
    BOOST_REQUIRE_EQUAL(discussions[0].permlink, p2.permlink());
    BOOST_REQUIRE_EQUAL(discussions[1].permlink, p1.permlink());
}

SCORUM_TEST_CASE(check_comments_should_not_be_returned)
{
    actor(initdelegate).give_sp(alice, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A", "D"]})";
    });
    auto c1 = p1.create_comment(bob, [](comment_operation& op) {
        op.permlink = "cpl";
        op.body = "cbody";
        op.json_metadata = R"({"tags":["A"]})";
    });
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["B","C"]})";
    });

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = false;
    q.tags = { "A", "B", "C", "D" };
    std::vector<discussion> discussions = _api.get_discussions_by_created(q);

    BOOST_REQUIRE_EQUAL(discussions.size(), 2);
    BOOST_REQUIRE_EQUAL(discussions[0].permlink, p2.permlink());
    BOOST_REQUIRE_EQUAL(discussions[1].permlink, p1.permlink());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(get_discussions_by_hot_tests, get_discussions_by_common)

SCORUM_TEST_CASE(should_return_voted_tags_union)
{
    actor(initdelegate).give_sp(alice, 1e9);
    actor(initdelegate).give_sp(bob, 1e9);
    actor(initdelegate).give_sp(sam, 1e9);

    auto p1 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl1";
        op.body = "body1";
        op.json_metadata = R"({"tags":["A","B","C"]})";
    });
    auto p2 = create_post(bob, [](comment_operation& op) {
        op.permlink = "pl2";
        op.body = "body2";
        op.json_metadata = R"({"tags":["C","D","E"]})";
    });
    auto p3 = create_post(alice, [](comment_operation& op) {
        op.permlink = "pl3";
        op.body = "body3";
        op.json_metadata = R"({"tags":["B","C","D","E"]})";
    });

    actor(sam).vote(p1.author(), p1.permlink());

    actor(sam).vote(p2.author(), p2.permlink());
    actor(bob).vote(p2.author(), p2.permlink());

    discussion_query q;
    q.limit = 100;
    q.tags_logical_and = true;
    q.tags = { "B", "C" };
    {
        std::vector<discussion> discussions = _api.get_discussions_by_hot(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 1);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p1.permlink());
    }

    actor(sam).vote(p3.author(), p3.permlink());
    actor(bob).vote(p3.author(), p3.permlink());
    actor(alice).vote(p3.author(), p3.permlink());

    {
        std::vector<discussion> discussions = _api.get_discussions_by_trending(q);

        BOOST_REQUIRE_EQUAL(discussions.size(), 2);
        BOOST_REQUIRE_EQUAL(discussions[0].permlink, p3.permlink());
        BOOST_REQUIRE_EQUAL(discussions[1].permlink, p1.permlink());
    }
}

BOOST_AUTO_TEST_SUITE_END()
}
