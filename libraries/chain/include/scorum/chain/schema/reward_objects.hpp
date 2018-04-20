#pragma once

#include <scorum/chain/schema/scorum_object_types.hpp>

#include <scorum/protocol/asset.hpp>
#include <scorum/protocol/types.hpp>

namespace scorum {
namespace chain {

using scorum::protocol::asset;
using scorum::protocol::asset_symbol_type;
using scorum::protocol::curve_id;

enum class reward_fund_asset_symbol : asset_symbol_type
{
    scr = SCORUM_SYMBOL,
    sp = SP_SYMBOL,
};

template <uint16_t ObjectType, reward_fund_asset_symbol SymbolType>
class reward_fund_object : public object<ObjectType, reward_fund_object<ObjectType, SymbolType>>
{
public:
    CHAINBASE_DEFAULT_CONSTRUCTOR(reward_fund_object)

    typedef typename object<ObjectType, reward_fund_object<ObjectType, SymbolType>>::id_type id_type;

    id_type id;

    asset activity_reward_balance = asset(0, (asset_symbol_type)SymbolType);
    fc::uint128_t recent_claims = 0;
    time_point_sec last_update;
    curve_id author_reward_curve;
    curve_id curation_reward_curve;
};

template <typename FundObjectType>
using reward_fund_index
    = shared_multi_index_container<FundObjectType,
                                   indexed_by<ordered_unique<tag<by_id>,
                                                             member<FundObjectType,
                                                                    typename FundObjectType::id_type,
                                                                    &FundObjectType::id>>>>;

using reward_fund_scr_object = reward_fund_object<reward_fund_scr_object_type, reward_fund_asset_symbol::scr>;
using reward_fund_sp_object = reward_fund_object<reward_fund_sp_object_type, reward_fund_asset_symbol::sp>;

using reward_fund_scr_index = reward_fund_index<reward_fund_scr_object>;
using reward_fund_sp_index = reward_fund_index<reward_fund_sp_object>;

} // namespace chain
} // namespace scorum

FC_REFLECT(scorum::chain::reward_fund_scr_object,
           (id)(activity_reward_balance)(recent_claims)(last_update)(author_reward_curve)(curation_reward_curve))
FC_REFLECT(scorum::chain::reward_fund_sp_object,
           (id)(activity_reward_balance)(recent_claims)(last_update)(author_reward_curve)(curation_reward_curve))

CHAINBASE_SET_INDEX_TYPE(scorum::chain::reward_fund_scr_object, scorum::chain::reward_fund_scr_index)
CHAINBASE_SET_INDEX_TYPE(scorum::chain::reward_fund_sp_object, scorum::chain::reward_fund_sp_index)