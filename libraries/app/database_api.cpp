#include <scorum/app/api_context.hpp>
#include <scorum/app/application.hpp>
#include <scorum/app/database_api.hpp>

#include <scorum/protocol/get_config.hpp>

#include <scorum/chain/util/reward.hpp>

#include <scorum/chain/services/account.hpp>
#include <scorum/chain/services/atomicswap.hpp>
#include <scorum/chain/services/budget.hpp>
#include <scorum/chain/services/comment.hpp>
#include <scorum/chain/services/development_committee.hpp>
#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/escrow.hpp>
#include <scorum/chain/services/proposal.hpp>
#include <scorum/chain/services/registration_committee.hpp>
#include <scorum/chain/services/reward_fund.hpp>
#include <scorum/chain/services/withdraw_scorumpower_route.hpp>
#include <scorum/chain/services/witness_schedule.hpp>
#include <scorum/chain/services/registration_pool.hpp>
#include <scorum/chain/services/reward_balancer.hpp>

#include <scorum/chain/schema/committee.hpp>
#include <scorum/chain/schema/proposal_object.hpp>
#include <scorum/chain/schema/withdraw_scorumpower_objects.hpp>
#include <scorum/chain/schema/registration_objects.hpp>
#include <scorum/chain/schema/budget_object.hpp>
#include <scorum/chain/schema/reward_balancer_object.hpp>
#include <scorum/chain/schema/scorum_objects.hpp>

#include <fc/bloom_filter.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/container/utils.hpp>

#include <boost/range/iterator_range.hpp>
#include <boost/algorithm/string.hpp>

#include <cctype>

#include <cfenv>
#include <iostream>

#define GET_REQUIRED_FEES_MAX_RECURSION (4)

namespace scorum {
namespace app {

constexpr uint32_t LOOKUP_LIMIT = 1000;
constexpr uint32_t GET_ACCOUNT_HISTORY_LIMIT = 10000;

class database_api_impl;

class database_api_impl : public std::enable_shared_from_this<database_api_impl>
{
public:
    database_api_impl(const scorum::app::api_context& ctx);
    ~database_api_impl();

    // Subscriptions
    void set_block_applied_callback(std::function<void(const variant& block_id)> cb);

    // Blocks and transactions
    optional<block_header> get_block_header(uint32_t block_num) const;
    optional<signed_block_api_obj> get_block(uint32_t block_num) const;
    std::vector<applied_operation> get_ops_in_block(uint32_t block_num, bool only_virtual) const;

    // Globals
    fc::variant_object get_config() const;
    dynamic_global_property_api_obj get_dynamic_global_properties() const;
    chain_id_type get_chain_id() const;

    // Keys
    std::vector<std::set<std::string>> get_key_references(std::vector<public_key_type> key) const;

    // Accounts
    std::vector<extended_account> get_accounts(const std::vector<std::string>& names) const;
    std::vector<account_id_type> get_account_references(account_id_type account_id) const;
    std::vector<optional<account_api_obj>> lookup_account_names(const std::vector<std::string>& account_names) const;
    std::set<std::string> lookup_accounts(const std::string& lower_bound_name, uint32_t limit) const;
    uint64_t get_account_count() const;

    // Budgets
    std::vector<budget_api_obj> get_budgets(const std::set<std::string>& names) const;
    std::set<std::string> lookup_budget_owners(const std::string& lower_bound_name, uint32_t limit) const;

    // Atomic Swap
    std::vector<atomicswap_contract_api_obj> get_atomicswap_contracts(const std::string& owner) const;
    atomicswap_contract_info_api_obj
    get_atomicswap_contract(const std::string& from, const std::string& to, const std::string& secret_hash) const;

    // Witnesses
    std::vector<optional<witness_api_obj>> get_witnesses(const std::vector<witness_id_type>& witness_ids) const;
    fc::optional<witness_api_obj> get_witness_by_account(const std::string& account_name) const;
    std::set<account_name_type> lookup_witness_accounts(const std::string& lower_bound_name, uint32_t limit) const;
    uint64_t get_witness_count() const;

    // Committee
    std::set<account_name_type> lookup_registration_committee_members(const std::string& lower_bound_name,
                                                                      uint32_t limit) const;

    std::set<account_name_type> lookup_development_committee_members(const std::string& lower_bound_name,
                                                                     uint32_t limit) const;

    std::vector<proposal_api_obj> lookup_proposals() const;

    // Authority / validation
    std::string get_transaction_hex(const signed_transaction& trx) const;
    std::set<public_key_type> get_required_signatures(const signed_transaction& trx,
                                                      const flat_set<public_key_type>& available_keys) const;
    std::set<public_key_type> get_potential_signatures(const signed_transaction& trx) const;
    bool verify_authority(const signed_transaction& trx) const;
    bool verify_account_authority(const std::string& name_or_id, const flat_set<public_key_type>& signers) const;

    // signal handlers
    void on_applied_block(const chain::signed_block& b);

    std::function<void(const fc::variant&)> _block_applied_callback;

    scorum::chain::database& _db;

    boost::signals2::scoped_connection _block_applied_connection;

    bool _disable_get_block = false;

    registration_committee_api_obj get_registration_committee() const;
    development_committee_api_obj get_development_committee() const;
};

applied_operation::applied_operation()
{
}

applied_operation::applied_operation(const operation_object& op_obj)
    : trx_id(op_obj.trx_id)
    , block(op_obj.block)
    , trx_in_block(op_obj.trx_in_block)
    , op_in_trx(op_obj.op_in_trx)
    , virtual_op(op_obj.virtual_op)
    , timestamp(op_obj.timestamp)
{
    // fc::raw::unpack( op_obj.serialized_op, op );     // g++ refuses to compile this as ambiguous
    op = fc::raw::unpack<operation>(op_obj.serialized_op);
}

void find_accounts(std::set<std::string>& accounts, const discussion& d)
{
    accounts.insert(d.author);
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Subscriptions                                                    //
//                                                                  //
//////////////////////////////////////////////////////////////////////

void database_api::set_block_applied_callback(std::function<void(const variant& block_id)> cb)
{
    my->_db.with_read_lock([&]() { my->set_block_applied_callback(cb); });
}

void database_api_impl::on_applied_block(const chain::signed_block& b)
{
    try
    {
        _block_applied_callback(fc::variant(signed_block_header(b)));
    }
    catch (...)
    {
        _block_applied_connection.release();
    }
}

void database_api_impl::set_block_applied_callback(std::function<void(const variant& block_header)> cb)
{
    _block_applied_callback = cb;
    _block_applied_connection = connect_signal(_db.applied_block, *this, &database_api_impl::on_applied_block);
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Constructors                                                     //
//                                                                  //
//////////////////////////////////////////////////////////////////////

database_api::database_api(const scorum::app::api_context& ctx)
    : my(new database_api_impl(ctx))
{
}

database_api::~database_api()
{
}

database_api_impl::database_api_impl(const scorum::app::api_context& ctx)
    : _db(*ctx.app.chain_database())
{
    wlog("creating database api ${x}", ("x", int64_t(this)));

    _disable_get_block = ctx.app._disable_get_block;
}

database_api_impl::~database_api_impl()
{
    elog("freeing database api ${x}", ("x", int64_t(this)));
}

void database_api::on_api_startup()
{
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Blocks and transactions                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<block_header> database_api::get_block_header(uint32_t block_num) const
{
    FC_ASSERT(!my->_disable_get_block, "get_block_header is disabled on this node.");

    return my->_db.with_read_lock([&]() { return my->get_block_header(block_num); });
}

optional<block_header> database_api_impl::get_block_header(uint32_t block_num) const
{
    auto result = _db.fetch_block_by_number(block_num);
    if (result)
        return *result;
    return {};
}

optional<signed_block_api_obj> database_api::get_block(uint32_t block_num) const
{
    FC_ASSERT(!my->_disable_get_block, "get_block is disabled on this node.");

    return my->_db.with_read_lock([&]() { return my->get_block(block_num); });
}

optional<signed_block_api_obj> database_api_impl::get_block(uint32_t block_num) const
{
    return _db.fetch_block_by_number(block_num);
}

std::vector<applied_operation> database_api::get_ops_in_block(uint32_t block_num, bool only_virtual) const
{
    return my->_db.with_read_lock([&]() { return my->get_ops_in_block(block_num, only_virtual); });
}

std::vector<applied_operation> database_api_impl::get_ops_in_block(uint32_t block_num, bool only_virtual) const
{
    const auto& idx = _db.get_index<operation_index>().indices().get<by_location>();
    auto itr = idx.lower_bound(block_num);
    std::vector<applied_operation> result;
    applied_operation temp;
    while (itr != idx.end() && itr->block == block_num)
    {
        temp = *itr;
        if (!only_virtual || is_virtual_operation(temp.op))
            result.push_back(temp);
        ++itr;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Globals                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////=

fc::variant_object database_api::get_config() const
{
    return my->_db.with_read_lock([&]() { return my->get_config(); });
}

fc::variant_object database_api_impl::get_config() const
{
    return scorum::protocol::get_config();
}

dynamic_global_property_api_obj database_api::get_dynamic_global_properties() const
{
    return my->_db.with_read_lock([&]() { return my->get_dynamic_global_properties(); });
}

chain_properties database_api::get_chain_properties() const
{
    return my->_db.with_read_lock(
        [&]() { return my->_db.obtain_service<dbs_dynamic_global_property>().get().median_chain_props; });
}

dynamic_global_property_api_obj database_api_impl::get_dynamic_global_properties() const
{
    dynamic_global_property_api_obj gpao;
    gpao = _db.obtain_service<dbs_dynamic_global_property>().get();

    if (_db.has_index<witness::reserve_ratio_index>())
    {
        const auto& r = _db.find(witness::reserve_ratio_id_type());

        if (BOOST_LIKELY(r != nullptr))
        {
            gpao = *r;
        }
    }

    gpao.registration_pool_balance = _db.obtain_service<dbs_registration_pool>().get().balance;
    gpao.fund_budget_balance = _db.obtain_service<dbs_budget>().get_fund_budget().balance;
    gpao.reward_pool_balance = _db.obtain_service<dbs_reward>().get().balance;
    gpao.content_reward_balance = _db.obtain_service<dbs_reward_fund>().get().activity_reward_balance_scr;

    return gpao;
}

chain_id_type database_api::get_chain_id() const
{
    return my->_db.with_read_lock([&]() { return my->get_chain_id(); });
}

chain_id_type database_api_impl::get_chain_id() const
{
    return _db.get_chain_id();
}

witness_schedule_api_obj database_api::get_witness_schedule() const
{
    return my->_db.with_read_lock([&]() { return my->_db.get(witness_schedule_id_type()); });
}

hardfork_version database_api::get_hardfork_version() const
{
    return my->_db.with_read_lock([&]() { return my->_db.get(hardfork_property_id_type()).current_hardfork_version; });
}

scheduled_hardfork database_api::get_next_scheduled_hardfork() const
{
    return my->_db.with_read_lock([&]() {
        scheduled_hardfork shf;
        const auto& hpo = my->_db.get(hardfork_property_id_type());
        shf.hf_version = hpo.next_hardfork;
        shf.live_time = hpo.next_hardfork_time;
        return shf;
    });
}

reward_fund_api_obj database_api::get_reward_fund() const
{
    return my->_db.with_read_lock([&]() {
        auto fund = my->_db.find<reward_fund_object>();
        FC_ASSERT(fund != nullptr, "reward fund object does not exist");

        return *fund;
    });
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Keys                                                             //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::vector<std::set<std::string>> database_api::get_key_references(std::vector<public_key_type> key) const
{
    return my->_db.with_read_lock([&]() { return my->get_key_references(key); });
}

/**
 *  @return all accounts that referr to the key or account id in their owner or active authorities.
 */
std::vector<std::set<std::string>> database_api_impl::get_key_references(std::vector<public_key_type> keys) const
{
    FC_ASSERT(false, "database_api::get_key_references has been deprecated. Please use "
                     "account_by_key_api::get_key_references instead.");
    std::vector<std::set<std::string>> final_result;
    return final_result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Accounts                                                         //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::vector<extended_account> database_api::get_accounts(const std::vector<std::string>& names) const
{
    return my->_db.with_read_lock([&]() { return my->get_accounts(names); });
}

std::vector<extended_account> database_api_impl::get_accounts(const std::vector<std::string>& names) const
{
    const auto& idx = _db.get_index<account_index>().indices().get<by_name>();
    const auto& vidx = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
    std::vector<extended_account> results;

    for (auto name : names)
    {
        auto itr = idx.find(name);
        if (itr != idx.end())
        {
            results.push_back(extended_account(*itr, _db));

            auto vitr = vidx.lower_bound(boost::make_tuple(itr->id, witness_id_type()));
            while (vitr != vidx.end() && vitr->account == itr->id)
            {
                results.back().witness_votes.insert(_db.get(vitr->witness).owner);
                ++vitr;
            }
        }
    }

    return results;
}

std::vector<account_id_type> database_api::get_account_references(account_id_type account_id) const
{
    return my->_db.with_read_lock([&]() { return my->get_account_references(account_id); });
}

std::vector<account_id_type> database_api_impl::get_account_references(account_id_type account_id) const
{
    /*const auto& idx = _db.get_index<account_index>();
    const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
    const auto& refs = aidx.get_secondary_index<scorum::chain::account_member_index>();
    auto itr = refs.account_to_account_memberships.find(account_id);
    std::vector<account_id_type> result;

    if( itr != refs.account_to_account_memberships.end() )
    {
       result.reserve( itr->second.size() );
       for( auto item : itr->second ) result.push_back(item);
    }
    return result;*/
    FC_ASSERT(false, "database_api::get_account_references --- Needs to be refactored for scorum.");
}

std::vector<optional<account_api_obj>>
database_api::lookup_account_names(const std::vector<std::string>& account_names) const
{
    return my->_db.with_read_lock([&]() { return my->lookup_account_names(account_names); });
}

std::vector<optional<account_api_obj>>
database_api_impl::lookup_account_names(const std::vector<std::string>& account_names) const
{
    std::vector<optional<account_api_obj>> result;
    result.reserve(account_names.size());

    for (auto& name : account_names)
    {
        auto itr = _db.find<account_object, by_name>(name);

        if (itr)
        {
            result.push_back(account_api_obj(*itr, _db));
        }
        else
        {
            result.push_back(optional<account_api_obj>());
        }
    }

    return result;
}

std::set<std::string> database_api::lookup_accounts(const std::string& lower_bound_name, uint32_t limit) const
{
    return my->_db.with_read_lock([&]() { return my->lookup_accounts(lower_bound_name, limit); });
}

std::set<std::string> database_api_impl::lookup_accounts(const std::string& lower_bound_name, uint32_t limit) const
{
    FC_ASSERT(limit <= LOOKUP_LIMIT);
    const auto& accounts_by_name = _db.get_index<account_index>().indices().get<by_name>();
    std::set<std::string> result;

    for (auto itr = accounts_by_name.lower_bound(lower_bound_name); limit-- && itr != accounts_by_name.end(); ++itr)
    {
        result.insert(itr->name);
    }

    return result;
}

uint64_t database_api::get_account_count() const
{
    return my->_db.with_read_lock([&]() { return my->get_account_count(); });
}

uint64_t database_api_impl::get_account_count() const
{
    return _db.get_index<account_index>().indices().size();
}

std::vector<owner_authority_history_api_obj> database_api::get_owner_history(const std::string& account) const
{
    return my->_db.with_read_lock([&]() {
        std::vector<owner_authority_history_api_obj> results;

        const auto& hist_idx = my->_db.get_index<owner_authority_history_index>().indices().get<by_account>();
        auto itr = hist_idx.lower_bound(account);

        while (itr != hist_idx.end() && itr->account == account)
        {
            results.push_back(owner_authority_history_api_obj(*itr));
            ++itr;
        }

        return results;
    });
}

optional<account_recovery_request_api_obj> database_api::get_recovery_request(const std::string& account) const
{
    return my->_db.with_read_lock([&]() {
        optional<account_recovery_request_api_obj> result;

        const auto& rec_idx = my->_db.get_index<account_recovery_request_index>().indices().get<by_account>();
        auto req = rec_idx.find(account);

        if (req != rec_idx.end())
            result = account_recovery_request_api_obj(*req);

        return result;
    });
}

optional<escrow_api_obj> database_api::get_escrow(const std::string& from, uint32_t escrow_id) const
{
    return my->_db.with_read_lock([&]() {
        optional<escrow_api_obj> result;

        try
        {
            result = my->_db.obtain_service<dbs_escrow>().get(from, escrow_id);
        }
        catch (...)
        {
        }

        return result;
    });
}

std::vector<withdraw_route> database_api::get_withdraw_routes(const std::string& account,
                                                              withdraw_route_type type) const
{
    return my->_db.with_read_lock([&]() {
        std::vector<withdraw_route> result;

        const auto& acc = my->_db.obtain_service<chain::dbs_account>().get_account(account);

        if (type == outgoing || type == all)
        {
            const auto& by_route
                = my->_db.get_index<withdraw_scorumpower_route_index>().indices().get<by_withdraw_route>();
            auto route = by_route.lower_bound(acc.id); // TODO: test get_withdraw_routes

            while (route != by_route.end() && is_equal_withdrawable_id(route->from_id, acc.id))
            {
                withdraw_route r;
                r.from_account = account;
                r.to_account = my->_db.get(route->to_id.get<account_id_type>()).name;
                r.percent = route->percent;
                r.auto_vest = route->auto_vest;

                result.push_back(r);

                ++route;
            }
        }

        if (type == incoming || type == all)
        {
            const auto& by_dest = my->_db.get_index<withdraw_scorumpower_route_index>().indices().get<by_destination>();
            auto route = by_dest.lower_bound(acc.id); // TODO: test get_withdraw_routes

            while (route != by_dest.end() && is_equal_withdrawable_id(route->to_id, acc.id))
            {
                withdraw_route r;
                r.from_account = my->_db.get(route->from_id.get<account_id_type>()).name;
                r.to_account = account;
                r.percent = route->percent;
                r.auto_vest = route->auto_vest;

                result.push_back(r);

                ++route;
            }
        }

        return result;
    });
}

optional<account_bandwidth_api_obj> database_api::get_account_bandwidth(const std::string& account,
                                                                        witness::bandwidth_type type) const
{
    optional<account_bandwidth_api_obj> result;

    if (my->_db.has_index<witness::account_bandwidth_index>())
    {
        auto band = my->_db.find<witness::account_bandwidth_object, witness::by_account_bandwidth_type>(
            boost::make_tuple(account, type));
        if (band != nullptr)
            result = *band;
    }

    return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Witnesses                                                        //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::vector<optional<witness_api_obj>>
database_api::get_witnesses(const std::vector<witness_id_type>& witness_ids) const
{
    return my->_db.with_read_lock([&]() { return my->get_witnesses(witness_ids); });
}

std::vector<optional<witness_api_obj>>
database_api_impl::get_witnesses(const std::vector<witness_id_type>& witness_ids) const
{
    std::vector<optional<witness_api_obj>> result;
    result.reserve(witness_ids.size());
    std::transform(witness_ids.begin(), witness_ids.end(), std::back_inserter(result),
                   [this](witness_id_type id) -> optional<witness_api_obj> {
                       if (auto o = _db.find(id))
                           return *o;
                       return {};
                   });
    return result;
}

fc::optional<witness_api_obj> database_api::get_witness_by_account(const std::string& account_name) const
{
    return my->_db.with_read_lock([&]() { return my->get_witness_by_account(account_name); });
}

std::vector<witness_api_obj> database_api::get_witnesses_by_vote(const std::string& from, uint32_t limit) const
{
    return my->_db.with_read_lock([&]() {
        // idump((from)(limit));
        FC_ASSERT(limit <= 100);

        std::vector<witness_api_obj> result;
        result.reserve(limit);

        const auto& name_idx = my->_db.get_index<witness_index>().indices().get<by_name>();
        const auto& vote_idx = my->_db.get_index<witness_index>().indices().get<by_vote_name>();

        auto itr = vote_idx.begin();
        if (from.size())
        {
            auto nameitr = name_idx.find(from);
            FC_ASSERT(nameitr != name_idx.end(), "invalid witness name ${n}", ("n", from));
            itr = vote_idx.iterator_to(*nameitr);
        }

        while (itr != vote_idx.end() && result.size() < limit && itr->votes > 0)
        {
            result.push_back(witness_api_obj(*itr));
            ++itr;
        }
        return result;
    });
}

fc::optional<witness_api_obj> database_api_impl::get_witness_by_account(const std::string& account_name) const
{
    const auto& idx = _db.get_index<witness_index>().indices().get<by_name>();
    auto itr = idx.find(account_name);
    if (itr != idx.end())
        return witness_api_obj(*itr);
    return {};
}

std::set<account_name_type> database_api::lookup_witness_accounts(const std::string& lower_bound_name,
                                                                  uint32_t limit) const
{
    return my->_db.with_read_lock([&]() { return my->lookup_witness_accounts(lower_bound_name, limit); });
}

std::set<account_name_type> database_api_impl::lookup_witness_accounts(const std::string& lower_bound_name,
                                                                       uint32_t limit) const
{
    FC_ASSERT(limit <= LOOKUP_LIMIT);
    const auto& witnesses_by_id = _db.get_index<witness_index>().indices().get<by_id>();

    // get all the names and look them all up, sort them, then figure out what
    // records to return.  This could be optimized, but we expect the
    // number of witnesses to be few and the frequency of calls to be rare
    std::set<account_name_type> witnesses_by_account_name;
    for (const witness_api_obj& witness : witnesses_by_id)
        if (witness.owner >= lower_bound_name) // we can ignore anything below lower_bound_name
            witnesses_by_account_name.insert(witness.owner);

    auto end_iter = witnesses_by_account_name.begin();
    while (end_iter != witnesses_by_account_name.end() && limit--)
        ++end_iter;
    witnesses_by_account_name.erase(end_iter, witnesses_by_account_name.end());
    return witnesses_by_account_name;
}

uint64_t database_api::get_witness_count() const
{
    return my->_db.with_read_lock([&]() { return my->get_witness_count(); });
}

uint64_t database_api_impl::get_witness_count() const
{
    return _db.get_index<witness_index>().indices().size();
}

std::set<account_name_type> database_api::lookup_registration_committee_members(const std::string& lower_bound_name,
                                                                                uint32_t limit) const
{

    return my->_db.with_read_lock([&]() { return my->lookup_registration_committee_members(lower_bound_name, limit); });
}

std::set<account_name_type> database_api::lookup_development_committee_members(const std::string& lower_bound_name,
                                                                               uint32_t limit) const
{
    return my->_db.with_read_lock([&]() { return my->lookup_development_committee_members(lower_bound_name, limit); });
}

std::set<account_name_type>
database_api_impl::lookup_registration_committee_members(const std::string& lower_bound_name, uint32_t limit) const
{
    FC_ASSERT(limit <= LOOKUP_LIMIT);

    return committee::lookup_members<registration_committee_member_index>(_db, lower_bound_name, limit);
}

std::set<account_name_type> database_api_impl::lookup_development_committee_members(const std::string& lower_bound_name,
                                                                                    uint32_t limit) const
{
    FC_ASSERT(limit <= LOOKUP_LIMIT);

    return committee::lookup_members<dev_committee_member_index>(_db, lower_bound_name, limit);
}

std::vector<proposal_api_obj> database_api::lookup_proposals() const
{
    return my->_db.with_read_lock([&]() { return my->lookup_proposals(); });
}

std::vector<proposal_api_obj> database_api_impl::lookup_proposals() const
{
    const auto& proposals_by_id = _db.get_index<proposal_object_index>().indices().get<by_id>();

    std::vector<proposal_api_obj> proposals;
    for (const proposal_api_obj& obj : proposals_by_id)
    {
        proposals.push_back(obj);
    }

    return proposals;
}

registration_committee_api_obj database_api::get_registration_committee() const
{
    return my->_db.with_read_lock([&]() { return my->get_registration_committee(); });
}

registration_committee_api_obj database_api_impl::get_registration_committee() const
{
    registration_committee_api_obj committee(_db.get(registration_pool_id_type()));

    return committee;
}

development_committee_api_obj database_api::get_development_committee() const
{
    return my->_db.with_read_lock([&]() { return my->get_development_committee(); });
}

development_committee_api_obj database_api_impl::get_development_committee() const
{
    development_committee_api_obj committee;
    committee = _db.get(dev_committee_id_type());

    return committee;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Authority / validation                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::string database_api::get_transaction_hex(const signed_transaction& trx) const
{
    return my->_db.with_read_lock([&]() { return my->get_transaction_hex(trx); });
}

std::string database_api_impl::get_transaction_hex(const signed_transaction& trx) const
{
    return fc::to_hex(fc::raw::pack(trx));
}

std::set<public_key_type> database_api::get_required_signatures(const signed_transaction& trx,
                                                                const flat_set<public_key_type>& available_keys) const
{
    return my->_db.with_read_lock([&]() { return my->get_required_signatures(trx, available_keys); });
}

std::set<public_key_type>
database_api_impl::get_required_signatures(const signed_transaction& trx,
                                           const flat_set<public_key_type>& available_keys) const
{
    //   wdump((trx)(available_keys));
    auto result = trx.get_required_signatures(
        get_chain_id(), available_keys,
        [&](const std::string& account_name) {
            return authority(_db.get<account_authority_object, by_account>(account_name).active);
        },
        [&](const std::string& account_name) {
            return authority(_db.get<account_authority_object, by_account>(account_name).owner);
        },
        [&](const std::string& account_name) {
            return authority(_db.get<account_authority_object, by_account>(account_name).posting);
        },
        SCORUM_MAX_SIG_CHECK_DEPTH);
    //   wdump((result));
    return result;
}

std::set<public_key_type> database_api::get_potential_signatures(const signed_transaction& trx) const
{
    return my->_db.with_read_lock([&]() { return my->get_potential_signatures(trx); });
}

std::set<public_key_type> database_api_impl::get_potential_signatures(const signed_transaction& trx) const
{
    //   wdump((trx));
    std::set<public_key_type> result;
    trx.get_required_signatures(
        get_chain_id(), flat_set<public_key_type>(),
        [&](account_name_type account_name) {
            const auto& auth = _db.get<account_authority_object, by_account>(account_name).active;
            for (const auto& k : auth.get_keys())
                result.insert(k);
            return authority(auth);
        },
        [&](account_name_type account_name) {
            const auto& auth = _db.get<account_authority_object, by_account>(account_name).owner;
            for (const auto& k : auth.get_keys())
                result.insert(k);
            return authority(auth);
        },
        [&](account_name_type account_name) {
            const auto& auth = _db.get<account_authority_object, by_account>(account_name).posting;
            for (const auto& k : auth.get_keys())
                result.insert(k);
            return authority(auth);
        },
        SCORUM_MAX_SIG_CHECK_DEPTH);

    //   wdump((result));
    return result;
}

bool database_api::verify_authority(const signed_transaction& trx) const
{
    return my->_db.with_read_lock([&]() { return my->verify_authority(trx); });
}

bool database_api_impl::verify_authority(const signed_transaction& trx) const
{
    trx.verify_authority(get_chain_id(),
                         [&](const std::string& account_name) {
                             return authority(_db.get<account_authority_object, by_account>(account_name).active);
                         },
                         [&](const std::string& account_name) {
                             return authority(_db.get<account_authority_object, by_account>(account_name).owner);
                         },
                         [&](const std::string& account_name) {
                             return authority(_db.get<account_authority_object, by_account>(account_name).posting);
                         },
                         SCORUM_MAX_SIG_CHECK_DEPTH);
    return true;
}

bool database_api::verify_account_authority(const std::string& name_or_id,
                                            const flat_set<public_key_type>& signers) const
{
    return my->_db.with_read_lock([&]() { return my->verify_account_authority(name_or_id, signers); });
}

bool database_api_impl::verify_account_authority(const std::string& name, const flat_set<public_key_type>& keys) const
{
    FC_ASSERT(name.size() > 0);
    auto account = _db.find<account_object, by_name>(name);
    FC_ASSERT(account, "no such account");

    /// reuse trx.verify_authority by creating a dummy transfer
    signed_transaction trx;
    transfer_operation op;
    op.from = account->name;
    trx.operations.emplace_back(op);

    return verify_authority(trx);
}

discussion database_api::get_content(const std::string& author, const std::string& permlink) const
{
    return my->_db.with_read_lock([&]() {
        const auto& by_permlink_idx = my->_db.get_index<comment_index>().indices().get<by_permlink>();
        auto itr = by_permlink_idx.find(boost::make_tuple(author, permlink));
        if (itr != by_permlink_idx.end())
        {
            discussion result(*itr);
            set_pending_payout(result);
            result.active_votes = get_active_votes(author, permlink);
            return result;
        }
        return discussion();
    });
}

std::vector<vote_state> database_api::get_active_votes(const std::string& author, const std::string& permlink) const
{
    return my->_db.with_read_lock([&]() {
        std::vector<vote_state> result;
        const auto& comment = my->_db.obtain_service<dbs_comment>().get(author, permlink);
        const auto& idx = my->_db.get_index<comment_vote_index>().indices().get<by_comment_voter>();
        comment_id_type cid(comment.id);
        auto itr = idx.lower_bound(cid);
        while (itr != idx.end() && itr->comment == cid)
        {
            const auto& vo = my->_db.get(itr->voter);
            vote_state vstate;
            vstate.voter = vo.name;
            vstate.weight = itr->weight;
            vstate.rshares = itr->rshares;
            vstate.percent = itr->vote_percent;
            vstate.time = itr->last_update;

            result.push_back(vstate);
            ++itr;
        }
        return result;
    });
}

std::vector<account_vote> database_api::get_account_votes(const std::string& voter) const
{
    return my->_db.with_read_lock([&]() {
        std::vector<account_vote> result;

        const auto& voter_acnt = my->_db.obtain_service<chain::dbs_account>().get_account(voter);
        const auto& idx = my->_db.get_index<comment_vote_index>().indices().get<by_voter_comment>();

        account_id_type aid(voter_acnt.id);
        auto itr = idx.lower_bound(aid);
        auto end = idx.upper_bound(aid);
        while (itr != end)
        {
            const auto& vo = my->_db.get(itr->comment);
            account_vote avote;
            avote.authorperm = vo.author + "/" + fc::to_string(vo.permlink);
            avote.weight = itr->weight;
            avote.rshares = itr->rshares;
            avote.percent = itr->vote_percent;
            avote.time = itr->last_update;
            result.push_back(avote);
            ++itr;
        }
        return result;
    });
}

u256 to256(const fc::uint128& t)
{
    u256 result(t.high_bits());
    result <<= 65;
    result += t.low_bits();
    return result;
}

void database_api::set_pending_payout(discussion& d) const
{
    const auto& cidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_comment>();
    auto itr = cidx.lower_bound(d.id);
    if (itr != cidx.end() && itr->comment == d.id)
    {
        d.promoted = asset(itr->promoted_balance, SCORUM_SYMBOL);
    }

    const auto& reward_fund_obj = my->_db.obtain_service<dbs_reward_fund>().get();

    asset pot = reward_fund_obj.activity_reward_balance_scr;
    u256 total_r2 = to256(reward_fund_obj.recent_claims);
    if (total_r2 > 0)
    {
        uint128_t vshares;
        vshares = d.net_rshares.value > 0
            ? scorum::chain::util::evaluate_reward_curve(d.net_rshares.value, reward_fund_obj.author_reward_curve)
            : 0;

        u256 r2 = to256(vshares); // to256(abs_net_rshares);
        r2 *= pot.amount.value;
        r2 /= total_r2;

        d.pending_payout_value = asset(static_cast<uint64_t>(r2), pot.symbol());
    }

    if (d.parent_author != SCORUM_ROOT_POST_PARENT_ACCOUNT)
        d.cashout_time = my->_db.calculate_discussion_payout_time(my->_db.get<comment_object>(d.id));

    if (d.body.size() > 1024 * 128)
        d.body = "body pruned due to size";
    if (d.parent_author.size() > 0 && d.body.size() > 1024 * 16)
        d.body = "comment pruned due to size";

    set_url(d);
}

void database_api::set_url(discussion& d) const
{
    const comment_api_obj root(my->_db.get<comment_object, by_id>(d.root_comment));
    d.url = "/" + root.category + "/@" + root.author + "/" + root.permlink;
    d.root_title = root.title;
    if (root.id != d.id)
        d.url += "#@" + d.author + "/" + d.permlink;
}

std::vector<discussion> database_api::get_content_replies(const std::string& author, const std::string& permlink) const
{
    return my->_db.with_read_lock([&]() {
        account_name_type acc_name = account_name_type(author);
        const auto& by_permlink_idx = my->_db.get_index<comment_index>().indices().get<by_parent>();
        auto itr = by_permlink_idx.find(boost::make_tuple(acc_name, permlink));
        std::vector<discussion> result;
        while (itr != by_permlink_idx.end() && itr->parent_author == author
               && fc::to_string(itr->parent_permlink) == permlink)
        {
            result.push_back(discussion(*itr));
            set_pending_payout(result.back());
            ++itr;
        }
        return result;
    });
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Budgets                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////
std::vector<budget_api_obj> database_api::get_budgets(const std::set<std::string>& names) const
{
    return my->_db.with_read_lock([&]() { return my->get_budgets(names); });
}

std::vector<budget_api_obj> database_api_impl::get_budgets(const std::set<std::string>& names) const
{
    FC_ASSERT(names.size() <= SCORUM_BUDGET_LIMIT_API_LIST_SIZE, "names size must be less or equal than ${1}",
              ("1", SCORUM_BUDGET_LIMIT_API_LIST_SIZE));

    std::vector<budget_api_obj> results;

    chain::dbs_budget& budget_service = _db.obtain_service<chain::dbs_budget>();

    for (const auto& name : names)
    {
        auto budgets = budget_service.get_budgets(name);
        if (results.size() + budgets.size() > SCORUM_BUDGET_LIMIT_API_LIST_SIZE)
        {
            break;
        }

        for (const chain::budget_object& budget : budgets)
        {
            results.push_back(budget_api_obj(budget));
        }
    }

    return results;
}

std::set<std::string> database_api::lookup_budget_owners(const std::string& lower_bound_name, uint32_t limit) const
{
    return my->_db.with_read_lock([&]() { return my->lookup_budget_owners(lower_bound_name, limit); });
}

std::set<std::string> database_api_impl::lookup_budget_owners(const std::string& lower_bound_name, uint32_t limit) const
{
    FC_ASSERT(limit <= SCORUM_BUDGET_LIMIT_API_LIST_SIZE, "limit must be less or equal than ${1}",
              ("1", SCORUM_BUDGET_LIMIT_API_LIST_SIZE));

    chain::dbs_budget& budget_service = _db.obtain_service<chain::dbs_budget>();

    return budget_service.lookup_budget_owners(lower_bound_name, limit);
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Atomic Swap                                                      //
//                                                                  //
//////////////////////////////////////////////////////////////////////
std::vector<atomicswap_contract_api_obj> database_api::get_atomicswap_contracts(const std::string& owner) const
{
    return my->_db.with_read_lock([&]() { return my->get_atomicswap_contracts(owner); });
}

std::vector<atomicswap_contract_api_obj> database_api_impl::get_atomicswap_contracts(const std::string& owner) const
{
    std::vector<atomicswap_contract_api_obj> results;

    chain::dbs_account& account_service = _db.obtain_service<chain::dbs_account>();
    const chain::account_object& owner_obj = account_service.get_account(owner);

    chain::dbs_atomicswap& atomicswap_service = _db.obtain_service<chain::dbs_atomicswap>();

    auto contracts = atomicswap_service.get_contracts(owner_obj);
    for (const chain::atomicswap_contract_object& contract : contracts)
    {
        results.push_back(atomicswap_contract_api_obj(contract));
    }

    return results;
}

atomicswap_contract_info_api_obj database_api::get_atomicswap_contract(const std::string& from,
                                                                       const std::string& to,
                                                                       const std::string& secret_hash) const
{
    return my->_db.with_read_lock([&]() { return my->get_atomicswap_contract(from, to, secret_hash); });
}

atomicswap_contract_info_api_obj database_api_impl::get_atomicswap_contract(const std::string& from,
                                                                            const std::string& to,
                                                                            const std::string& secret_hash) const
{
    atomicswap_contract_info_api_obj result;

    chain::dbs_account& account_service = _db.obtain_service<chain::dbs_account>();
    const chain::account_object& from_obj = account_service.get_account(from);
    const chain::account_object& to_obj = account_service.get_account(to);

    chain::dbs_atomicswap& atomicswap_service = _db.obtain_service<chain::dbs_atomicswap>();

    const chain::atomicswap_contract_object& contract = atomicswap_service.get_contract(from_obj, to_obj, secret_hash);

    return atomicswap_contract_info_api_obj(contract);
}

/**
 *  This method can be used to fetch replies to an account.
 *
 *  The first call should be (account_to_retrieve replies, "", limit)
 *  Subsequent calls should be (last_author, last_permlink, limit)
 */
std::vector<discussion> database_api::get_replies_by_last_update(account_name_type start_parent_author,
                                                                 const std::string& start_permlink,
                                                                 uint32_t limit) const
{
    return my->_db.with_read_lock([&]() {
        std::vector<discussion> result;

#ifndef IS_LOW_MEM
        FC_ASSERT(limit <= 100);
        const auto& last_update_idx = my->_db.get_index<comment_index>().indices().get<by_last_update>();
        auto itr = last_update_idx.begin();
        const account_name_type* parent_author = &start_parent_author;

        if (start_permlink.size())
        {
            const auto& comment = my->_db.obtain_service<dbs_comment>().get(start_parent_author, start_permlink);
            itr = last_update_idx.iterator_to(comment);
            parent_author = &comment.parent_author;
        }
        else if (start_parent_author.size())
        {
            itr = last_update_idx.lower_bound(start_parent_author);
        }

        result.reserve(limit);

        while (itr != last_update_idx.end() && result.size() < limit && itr->parent_author == *parent_author)
        {
            result.push_back(*itr);
            set_pending_payout(result.back());
            result.back().active_votes = get_active_votes(itr->author, fc::to_string(itr->permlink));
            ++itr;
        }

#endif
        return result;
    });
}

std::vector<std::pair<std::string, uint32_t>> database_api::get_tags_used_by_author(const std::string& author) const
{
    return my->_db.with_read_lock([&]() {
        try
        {
            const auto& acnt = my->_db.obtain_service<dbs_account>().get_account(author);

            const auto& tidx
                = my->_db.get_index<tags::author_tag_stats_index>().indices().get<tags::by_author_posts_tag>();
            auto itr = tidx.lower_bound(boost::make_tuple(acnt.id, 0));
            std::vector<std::pair<std::string, uint32_t>> result;
            while (itr != tidx.end() && itr->author == acnt.id && result.size() < LOOKUP_LIMIT)
            {
                result.push_back(std::make_pair(itr->tag, itr->total_posts));
                ++itr;
            }
            return result;
        }
        FC_CAPTURE_AND_RETHROW()
    });
}

std::vector<tag_api_obj> database_api::get_trending_tags(const std::string& after, uint32_t limit) const
{
    return my->_db.with_read_lock([&]() {
        limit = std::min(limit, uint32_t(LOOKUP_LIMIT));
        std::vector<tag_api_obj> result;
        result.reserve(limit);

        const auto& nidx = my->_db.get_index<tags::tag_stats_index>().indices().get<tags::by_tag>();

        const auto& ridx = my->_db.get_index<tags::tag_stats_index>().indices().get<tags::by_trending>();
        auto itr = ridx.begin();
        if (after != "" && nidx.size())
        {
            auto nitr = nidx.lower_bound(after);
            if (nitr == nidx.end())
                itr = ridx.end();
            else
                itr = ridx.iterator_to(*nitr);
        }

        while (itr != ridx.end() && result.size() < limit)
        {
            result.push_back(tag_api_obj(*itr));
            ++itr;
        }
        return result;
    });
}

discussion database_api::get_discussion(comment_id_type id, uint32_t truncate_body) const
{
    discussion d = my->_db.get(id);
    set_url(d);
    set_pending_payout(d);
    d.active_votes = get_active_votes(d.author, d.permlink);
    d.body_length = d.body.size();
    if (truncate_body)
    {
        d.body = d.body.substr(0, truncate_body);

        if (!fc::is_utf8(d.body))
            d.body = fc::prune_invalid_utf8(d.body);
    }
    return d;
}

template <typename Index, typename StartItr>
std::vector<discussion> database_api::get_discussions(const discussion_query& query,
                                                      const std::string& tag,
                                                      comment_id_type parent,
                                                      const Index& tidx,
                                                      StartItr tidx_itr,
                                                      uint32_t truncate_body,
                                                      const std::function<bool(const comment_api_obj&)>& filter,
                                                      const std::function<bool(const comment_api_obj&)>& exit,
                                                      const std::function<bool(const tags::tag_object&)>& tag_exit,
                                                      bool ignore_parent) const
{
    // idump((query));
    std::vector<discussion> result;

    const auto& cidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_comment>();
    comment_id_type start;

    if (query.start_author && query.start_permlink)
    {
        start = my->_db.obtain_service<dbs_comment>().get(*query.start_author, *query.start_permlink).id;
        auto itr = cidx.find(start);
        while (itr != cidx.end() && itr->comment == start)
        {
            if (itr->tag == tag)
            {
                tidx_itr = tidx.iterator_to(*itr);
                break;
            }
            ++itr;
        }
    }

    uint32_t count = query.limit;
    uint64_t itr_count = 0;
    uint64_t filter_count = 0;
    uint64_t exc_count = 0;
    uint64_t max_itr_count = 10 * query.limit;
    while (count > 0 && tidx_itr != tidx.end())
    {
        ++itr_count;
        if (itr_count > max_itr_count)
        {
            wlog("Maximum iteration count exceeded serving query: ${q}", ("q", query));
            wlog("count=${count}   itr_count=${itr_count}   filter_count=${filter_count}   exc_count=${exc_count}",
                 ("count", count)("itr_count", itr_count)("filter_count", filter_count)("exc_count", exc_count));
            break;
        }
        if (tidx_itr->tag != tag || (!ignore_parent && tidx_itr->parent != parent))
            break;
        try
        {
            result.push_back(get_discussion(tidx_itr->comment, truncate_body));
            result.back().promoted = asset(tidx_itr->promoted_balance, SCORUM_SYMBOL);

            if (filter(result.back()))
            {
                result.pop_back();
                ++filter_count;
            }
            else if (exit(result.back()) || tag_exit(*tidx_itr))
            {
                result.pop_back();
                break;
            }
            else
                --count;
        }
        catch (const fc::exception& e)
        {
            ++exc_count;
            edump((e.to_detail_string()));
        }
        ++tidx_itr;
    }
    return result;
}

comment_id_type database_api::get_parent(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        comment_id_type parent;
        if (query.parent_author && query.parent_permlink)
        {
            parent = my->_db.obtain_service<dbs_comment>().get(*query.parent_author, *query.parent_permlink).id;
        }
        return parent;
    });
}

std::vector<discussion> database_api::get_discussions_by_payout(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_net_rshares>();
        auto tidx_itr = tidx.lower_bound(tag);

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body,
                               [](const comment_api_obj& c) { return c.net_rshares <= 0; }, exit_default,
                               tag_exit_default, true);
    });
}

std::vector<discussion> database_api::get_post_discussions_by_payout(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = comment_id_type();

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_reward_fund_net_rshares>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, true));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body,
                               [](const comment_api_obj& c) { return c.net_rshares <= 0; }, exit_default,
                               tag_exit_default, true);
    });
}

std::vector<discussion> database_api::get_comment_discussions_by_payout(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = comment_id_type(1);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_reward_fund_net_rshares>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, false));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body,
                               [](const comment_api_obj& c) { return c.net_rshares <= 0; }, exit_default,
                               tag_exit_default, true);
    });
}

std::vector<discussion> database_api::get_discussions_by_promoted(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_promoted>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, share_type(SCORUM_MAX_SHARE_SUPPLY)));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body, filter_default, exit_default,
                               [](const tags::tag_object& t) { return t.promoted_balance == 0; });
    });
}

std::vector<discussion> database_api::get_discussions_by_trending(const discussion_query& query) const
{

    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_trending>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, std::numeric_limits<double>::max()));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body,
                               [](const comment_api_obj& c) { return c.net_rshares <= 0; });
    });
}

std::vector<discussion> database_api::get_discussions_by_created(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_created>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, fc::time_point_sec::maximum()));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body);
    });
}

std::vector<discussion> database_api::get_discussions_by_active(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_active>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, fc::time_point_sec::maximum()));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body);
    });
}

std::vector<discussion> database_api::get_discussions_by_cashout(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        std::vector<discussion> result;

        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_cashout>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, fc::time_point::now() - fc::minutes(60)));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body,
                               [](const comment_api_obj& c) { return c.net_rshares < 0; });
    });
}

std::vector<discussion> database_api::get_discussions_by_votes(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_net_votes>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, std::numeric_limits<int32_t>::max()));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body);
    });
}

std::vector<discussion> database_api::get_discussions_by_children(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_children>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, std::numeric_limits<int32_t>::max()));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body);
    });
}

std::vector<discussion> database_api::get_discussions_by_hot(const discussion_query& query) const
{

    return my->_db.with_read_lock([&]() {
        query.validate();
        auto tag = fc::to_lower(query.tag);
        auto parent = get_parent(query);

        const auto& tidx = my->_db.get_index<tags::tag_index>().indices().get<tags::by_parent_hot>();
        auto tidx_itr = tidx.lower_bound(boost::make_tuple(tag, parent, std::numeric_limits<double>::max()));

        return get_discussions(query, tag, parent, tidx, tidx_itr, query.truncate_body,
                               [](const comment_api_obj& c) { return c.net_rshares <= 0; });
    });
}

std::vector<discussion> database_api::get_discussions_by_comments(const discussion_query& query) const
{
    return my->_db.with_read_lock([&]() {
        std::vector<discussion> result;
#ifndef IS_LOW_MEM
        query.validate();
        FC_ASSERT(query.start_author, "Must get comments for a specific author");
        auto start_author = *(query.start_author);
        auto start_permlink = query.start_permlink ? *(query.start_permlink) : "";

        const auto& c_idx = my->_db.get_index<comment_index>().indices().get<by_permlink>();
        const auto& t_idx = my->_db.get_index<comment_index>().indices().get<by_author_last_update>();
        auto comment_itr = t_idx.lower_bound(start_author);

        if (start_permlink.size())
        {
            auto start_c = c_idx.find(boost::make_tuple(start_author, start_permlink));
            FC_ASSERT(start_c != c_idx.end(), "Comment is not in account's comments");
            comment_itr = t_idx.iterator_to(*start_c);
        }

        result.reserve(query.limit);

        while (result.size() < query.limit && comment_itr != t_idx.end())
        {
            if (comment_itr->author != start_author)
                break;
            if (comment_itr->parent_author.size() > 0)
            {
                try
                {
                    result.push_back(get_discussion(comment_itr->id));
                }
                catch (const fc::exception& e)
                {
                    edump((e.to_detail_string()));
                }
            }

            ++comment_itr;
        }
#endif
        return result;
    });
}

/**
 *  This call assumes root already stored as part of state, it will
 *  modify root.replies to contain links to the reply posts and then
 *  add the reply discussions to the state. This method also fetches
 *  any accounts referenced by authors.
 *
 */
void database_api::recursively_fetch_content(state& _state,
                                             discussion& root,
                                             std::set<std::string>& referenced_accounts) const
{
    return my->_db.with_read_lock([&]() {
        try
        {
            if (root.author.size())
                referenced_accounts.insert(root.author);

            auto replies = get_content_replies(root.author, root.permlink);
            for (auto& r : replies)
            {
                try
                {
                    recursively_fetch_content(_state, r, referenced_accounts);
                    root.replies.push_back(r.author + "/" + r.permlink);
                    _state.content[r.author + "/" + r.permlink] = std::move(r);
                    if (r.author.size())
                        referenced_accounts.insert(r.author);
                }
                catch (const fc::exception& e)
                {
                    edump((e.to_detail_string()));
                }
            }
        }
        FC_CAPTURE_AND_RETHROW((root.author)(root.permlink))
    });
}

std::vector<account_name_type> database_api::get_active_witnesses() const
{
    return my->_db.with_read_lock([&]() {
        const auto& wso = my->_db.obtain_service<chain::dbs_witness_schedule>().get();
        size_t n = wso.current_shuffled_witnesses.size();
        std::vector<account_name_type> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++)
            result.push_back(wso.current_shuffled_witnesses[i]);
        return result;
    });
}

std::vector<discussion> database_api::get_discussions_by_author_before_date(const std::string& author,
                                                                            const std::string& start_permlink,
                                                                            time_point_sec before_date,
                                                                            uint32_t limit) const
{
    return my->_db.with_read_lock([&]() {
        try
        {
            std::vector<discussion> result;
#ifndef IS_LOW_MEM
            FC_ASSERT(limit <= 100);
            result.reserve(limit);
            uint32_t count = 0;
            const auto& didx = my->_db.get_index<comment_index>().indices().get<by_author_last_update>();

            if (before_date == time_point_sec())
                before_date = time_point_sec::maximum();

            auto itr = didx.lower_bound(boost::make_tuple(author, time_point_sec::maximum()));
            if (start_permlink.size())
            {
                const auto& comment = my->_db.obtain_service<dbs_comment>().get(author, start_permlink);
                if (comment.created < before_date)
                    itr = didx.iterator_to(comment);
            }

            while (itr != didx.end() && itr->author == author && count < limit)
            {
                if (itr->parent_author.size() == 0)
                {
                    result.push_back(*itr);
                    set_pending_payout(result.back());
                    result.back().active_votes = get_active_votes(itr->author, fc::to_string(itr->permlink));
                    ++count;
                }
                ++itr;
            }

#endif
            return result;
        }
        FC_CAPTURE_AND_RETHROW((author)(start_permlink)(before_date)(limit))
    });
}

std::vector<scorumpower_delegation_api_obj>
database_api::get_scorumpower_delegations(const std::string& account, const std::string& from, uint32_t limit) const
{
    FC_ASSERT(limit <= LOOKUP_LIMIT);

    return my->_db.with_read_lock([&]() {
        std::vector<scorumpower_delegation_api_obj> result;
        result.reserve(limit);

        const auto& delegation_idx = my->_db.get_index<scorumpower_delegation_index, by_delegation>();
        auto itr = delegation_idx.lower_bound(boost::make_tuple(account, from));
        while (result.size() < limit && itr != delegation_idx.end() && itr->delegator == account)
        {
            result.push_back(*itr);
            ++itr;
        }

        return result;
    });
}

std::vector<scorumpower_delegation_expiration_api_obj> database_api::get_expiring_scorumpower_delegations(
    const std::string& account, time_point_sec from, uint32_t limit) const
{
    FC_ASSERT(limit <= LOOKUP_LIMIT);

    return my->_db.with_read_lock([&]() {
        std::vector<scorumpower_delegation_expiration_api_obj> result;
        result.reserve(limit);

        const auto& exp_idx = my->_db.get_index<scorumpower_delegation_expiration_index, by_account_expiration>();
        auto itr = exp_idx.lower_bound(boost::make_tuple(account, from));
        while (result.size() < limit && itr != exp_idx.end() && itr->delegator == account)
        {
            result.push_back(*itr);
            ++itr;
        }

        return result;
    });
}

state database_api::get_state(std::string path) const
{
    return my->_db.with_read_lock([&]() {
        state _state;
        _state.props = get_dynamic_global_properties();
        _state.current_route = path;

        try
        {
            if (path.size() && path[0] == '/')
                path = path.substr(1); /// remove '/' from front

            if (!path.size())
                path = "trending";

            /// FETCH CATEGORY STATE
            auto trending_tags = get_trending_tags(std::string(), 50);
            for (const auto& t : trending_tags)
            {
                _state.tag_idx.trending.push_back(std::string(t.name));
            }
            /// END FETCH CATEGORY STATE

            std::set<std::string> accounts;

            std::vector<std::string> part;
            part.reserve(4);
            boost::split(part, path, boost::is_any_of("/"));
            part.resize(std::max(part.size(), size_t(4))); // at least 4

            auto tag = fc::to_lower(part[1]);

            if (part[0].size() && part[0][0] == '@')
            {
                auto acnt = part[0].substr(1);
                _state.accounts[acnt]
                    = extended_account(my->_db.obtain_service<chain::dbs_account>().get_account(acnt), my->_db);
                _state.accounts[acnt].tags_usage = get_tags_used_by_author(acnt);

                auto& eacnt = _state.accounts[acnt];
                if (part[1] == "transfers")
                {
                    // TODO: rework this garbage method - split it into sensible parts
                    // auto history = get_account_history(acnt, uint64_t(-1), 10000);
                    // for (auto& item : history)
                    //{
                    //    switch (item.second.op.which())
                    //    {
                    //    case operation::tag<transfer_to_scorumpower_operation>::value:
                    //    case operation::tag<withdraw_scorumpower_operation>::value:
                    //    case operation::tag<transfer_operation>::value:
                    //    case operation::tag<author_reward_operation>::value:
                    //    case operation::tag<curation_reward_operation>::value:
                    //    case operation::tag<comment_benefactor_reward_operation>::value:
                    //    case operation::tag<escrow_transfer_operation>::value:
                    //    case operation::tag<escrow_approve_operation>::value:
                    //    case operation::tag<escrow_dispute_operation>::value:
                    //    case operation::tag<escrow_release_operation>::value:
                    //        eacnt.transfer_history[item.first] = item.second;
                    //        break;
                    //    case operation::tag<comment_operation>::value:
                    //        //   eacnt.post_history[item.first] =  item.second;
                    //        break;
                    //    case operation::tag<vote_operation>::value:
                    //    case operation::tag<account_witness_vote_operation>::value:
                    //    case operation::tag<account_witness_proxy_operation>::value:
                    //        //   eacnt.vote_history[item.first] =  item.second;
                    //        break;
                    //    case operation::tag<account_create_operation>::value:
                    //    case operation::tag<account_update_operation>::value:
                    //    case operation::tag<witness_update_operation>::value:
                    //    case operation::tag<producer_reward_operation>::value:
                    //    default:
                    //        eacnt.other_history[item.first] = item.second;
                    //    }
                    //}
                }
                else if (part[1] == "recent-replies")
                {
                    auto replies = get_replies_by_last_update(acnt, "", 50);
                    eacnt.recent_replies = std::vector<std::string>();
                    for (const auto& reply : replies)
                    {
                        auto reply_ref = reply.author + "/" + reply.permlink;
                        _state.content[reply_ref] = reply;

                        eacnt.recent_replies->push_back(reply_ref);
                    }
                }
                else if (part[1] == "posts" || part[1] == "comments")
                {
#ifndef IS_LOW_MEM
                    int count = 0;
                    const auto& pidx = my->_db.get_index<comment_index>().indices().get<by_author_last_update>();
                    auto itr = pidx.lower_bound(acnt);
                    eacnt.comments = std::vector<std::string>();

                    while (itr != pidx.end() && itr->author == acnt && count < 20)
                    {
                        if (itr->parent_author.size())
                        {
                            const auto link = acnt + "/" + fc::to_string(itr->permlink);
                            eacnt.comments->push_back(link);
                            _state.content[link] = *itr;
                            set_pending_payout(_state.content[link]);
                            ++count;
                        }

                        ++itr;
                    }
#endif
                }
            }
            /// pull a complete discussion
            else if (part[1].size() && part[1][0] == '@')
            {
                auto account = part[1].substr(1);
                auto slug = part[2];

                auto key = account + "/" + slug;
                auto dis = get_content(account, slug);

                recursively_fetch_content(_state, dis, accounts);
                _state.content[key] = std::move(dis);
            }
            else if (part[0] == "witnesses" || part[0] == "~witnesses")
            {
                auto wits = get_witnesses_by_vote("", 50);
                for (const auto& w : wits)
                {
                    _state.witnesses[w.owner] = w;
                }
            }
            else if (part[0] == "trending")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_trending(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.trending.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "payout")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_post_discussions_by_payout(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.payout.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "payout_comments")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_comment_discussions_by_payout(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.payout_comments.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "promoted")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_promoted(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.promoted.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "responses")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_children(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.responses.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (!part[0].size() || part[0] == "hot")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_hot(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.hot.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (!part[0].size() || part[0] == "promoted")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_promoted(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.promoted.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "votes")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_votes(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.votes.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "cashout")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_cashout(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.cashout.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "active")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_active(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.active.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "created")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_created(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.created.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "recent")
            {
                discussion_query q;
                q.tag = tag;
                q.limit = 20;
                q.truncate_body = 1024;
                auto trending_disc = get_discussions_by_created(q);

                auto& didx = _state.discussion_idx[tag];
                for (const auto& d : trending_disc)
                {
                    auto key = d.author + "/" + d.permlink;
                    didx.created.push_back(key);
                    if (d.author.size())
                        accounts.insert(d.author);
                    _state.content[key] = std::move(d);
                }
            }
            else if (part[0] == "tags")
            {
                _state.tag_idx.trending.clear();
                auto trending_tags = get_trending_tags(std::string(), 250);
                for (const auto& t : trending_tags)
                {
                    std::string name = t.name;
                    _state.tag_idx.trending.push_back(name);
                    _state.tags[name] = t;
                }
            }
            else
            {
                elog("What... no matches");
            }

            chain::dbs_account& account_service = my->_db.obtain_service<chain::dbs_account>();
            for (const auto& a : accounts)
            {
                _state.accounts.erase("");
                _state.accounts[a] = extended_account(account_service.get_account(a), my->_db);
            }
            for (auto& d : _state.content)
            {
                d.second.active_votes = get_active_votes(d.second.author, d.second.permlink);
            }

            _state.witness_schedule = my->_db.obtain_service<dbs_witness_schedule>().get();
        }
        catch (const fc::exception& e)
        {
            _state.error = e.to_detail_string();
        }
        return _state;
    });
}

annotated_signed_transaction database_api::get_transaction(transaction_id_type id) const
{
#ifdef SKIP_BY_TX_ID
    FC_ASSERT(false, "This node's operator has disabled operation indexing by transaction_id");
#else
    return my->_db.with_read_lock([&]() {
        const auto& idx = my->_db.get_index<operation_index>().indices().get<by_transaction_id>();
        auto itr = idx.lower_bound(id);
        if (itr != idx.end() && itr->trx_id == id)
        {
            auto blk = my->_db.fetch_block_by_number(itr->block);
            FC_ASSERT(blk.valid());
            FC_ASSERT(blk->transactions.size() > itr->trx_in_block);
            annotated_signed_transaction result = blk->transactions[itr->trx_in_block];
            result.block_num = itr->block;
            result.transaction_num = itr->trx_in_block;
            return result;
        }
        FC_ASSERT(false, "Unknown Transaction ${t}", ("t", id));
    });
#endif
}
} // namespace app
} // namespace scorum
