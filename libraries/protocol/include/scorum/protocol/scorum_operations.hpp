#pragma once
#include <scorum/protocol/base.hpp>
#include <scorum/protocol/block_header.hpp>
#include <scorum/protocol/asset.hpp>
#include <scorum/protocol/chain_properties.hpp>
#include <scorum/protocol/comment.hpp>
#include <scorum/protocol/types.hpp>

#include <scorum/protocol/proposal_operations.hpp>

#include <fc/crypto/ripemd160.hpp>

namespace scorum {
namespace protocol {

struct account_create_operation : public base_operation
{
    asset fee = asset(0, SCORUM_SYMBOL);
    account_name_type creator;
    account_name_type new_account_name;
    authority owner;
    authority active;
    authority posting;
    public_key_type memo_key;
    std::string json_metadata;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(creator);
    }
};

struct account_create_with_delegation_operation : public base_operation
{
    asset fee = asset(0, SCORUM_SYMBOL);
    asset delegation = asset(0, SP_SYMBOL);
    account_name_type creator;
    account_name_type new_account_name;
    authority owner;
    authority active;
    authority posting;
    public_key_type memo_key;
    std::string json_metadata;

    extensions_type extensions;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(creator);
    }
};

struct account_create_by_committee_operation : public base_operation
{
    account_name_type creator;
    account_name_type new_account_name;
    authority owner;
    authority active;
    authority posting;
    public_key_type memo_key;
    std::string json_metadata;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(creator);
    }
};

struct account_update_operation : public base_operation
{
    account_name_type account;
    optional<authority> owner;
    optional<authority> active;
    optional<authority> posting;
    public_key_type memo_key;
    std::string json_metadata;

    void validate() const;

    void get_required_owner_authorities(flat_set<account_name_type>& a) const
    {
        if (owner)
            a.insert(account);
    }

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        if (!owner)
            a.insert(account);
    }
};

struct comment_operation : public base_operation
{
    account_name_type parent_author;
    std::string parent_permlink;

    account_name_type author;
    std::string permlink;

    std::string title;
    std::string body;
    std::string json_metadata;

    void validate() const;
    void get_required_posting_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(author);
    }
};

/**
 *  Authors of posts may not want all of the benefits that come from creating a post. This
 *  operation allows authors to update properties associated with their post.
 *
 *  The max_accepted_payout may be decreased, but never increased.
 *
 */
struct comment_options_operation : public base_operation
{
    account_name_type author;
    std::string permlink;

    asset max_accepted_payout
        = asset::maximum(SCORUM_SYMBOL); /// SCR value of the maximum payout this post will receive
    bool allow_votes = true; /// allows a post to receive votes;
    bool allow_curation_rewards = true; /// allows voters to receive curation rewards. Rewards return to reward fund.
    comment_options_extensions_type extensions;

    void validate() const;
    void get_required_posting_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(author);
    }
};

struct prove_authority_operation : public base_operation
{
    account_name_type challenged;
    bool require_owner = false;

    void validate() const;

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        if (!require_owner)
            a.insert(challenged);
    }
    void get_required_owner_authorities(flat_set<account_name_type>& a) const
    {
        if (require_owner)
            a.insert(challenged);
    }
};

struct delete_comment_operation : public base_operation
{
    account_name_type author;
    std::string permlink;

    void validate() const;
    void get_required_posting_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(author);
    }
};

struct vote_operation : public base_operation
{
    account_name_type voter;
    account_name_type author;
    std::string permlink;
    int16_t weight = 0;

    void validate() const;
    void get_required_posting_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(voter);
    }
};

/**
 * @ingroup operations
 *
 * @brief Transfers SCR from one account to another.
 */
struct transfer_operation : public base_operation
{
    account_name_type from;
    /// Account to transfer asset to
    account_name_type to;
    /// The amount of asset to transfer from @ref from to @ref to
    asset amount = asset(0, SCORUM_SYMBOL);

    /// The memo is plain-text, any encryption on the memo is up to
    /// a higher level protocol.
    std::string memo;

    void validate() const;

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(from);
    }
};

/**
 *  The purpose of this operation is to enable someone to send money contingently to
 *  another individual. The funds leave the *from* account and go into a temporary balance
 *  where they are held until *from* releases it to *to* or *to* refunds it to *from*.
 *
 *  In the event of a dispute the *agent* can divide the funds between the to/from account.
 *  Disputes can be raised any time before or on the dispute deadline time, after the escrow
 *  has been approved by all parties.
 *
 *  This operation only creates a proposed escrow transfer. Both the *agent* and *to* must
 *  agree to the terms of the arrangement by approving the escrow.
 *
 *  The escrow agent is paid the fee on approval of all parties. It is up to the escrow agent
 *  to determine the fee.
 *
 *  Escrow transactions are uniquely identified by 'from' and 'escrow_id', the 'escrow_id' is defined
 *  by the sender.
 */
struct escrow_transfer_operation : public base_operation
{
    account_name_type from;
    account_name_type to;
    account_name_type agent;
    uint32_t escrow_id = 30;

    asset scorum_amount = asset(0, SCORUM_SYMBOL);
    asset fee = asset(0, SCORUM_SYMBOL);

    time_point_sec ratification_deadline;
    time_point_sec escrow_expiration;

    std::string json_meta;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(from);
    }
};

/**
 *  The agent and to accounts must approve an escrow transaction for it to be valid on
 *  the blockchain. Once a part approves the escrow, the cannot revoke their approval.
 *  Subsequent escrow approve operations, regardless of the approval, will be rejected.
 */
struct escrow_approve_operation : public base_operation
{
    account_name_type from;
    account_name_type to;
    account_name_type agent;
    account_name_type who; // Either to or agent

    uint32_t escrow_id = 30;
    bool approve = true;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(who);
    }
};

/**
 *  If either the sender or receiver of an escrow payment has an issue, they can
 *  raise it for dispute. Once a payment is in dispute, the agent has authority over
 *  who gets what.
 */
struct escrow_dispute_operation : public base_operation
{
    account_name_type from;
    account_name_type to;
    account_name_type agent;
    account_name_type who;

    uint32_t escrow_id = 30;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(who);
    }
};

/**
 *  This operation can be used by anyone associated with the escrow transfer to
 *  release funds if they have permission.
 *
 *  The permission scheme is as follows:
 *  If there is no dispute and escrow has not expired, either party can release funds to the other.
 *  If escrow expires and there is no dispute, either party can release funds to either party.
 *  If there is a dispute regardless of expiration, the agent can release funds to either party
 *     following whichever agreement was in place between the parties.
 */
struct escrow_release_operation : public base_operation
{
    account_name_type from;
    account_name_type to; ///< the original 'to'
    account_name_type agent;
    account_name_type who; ///< the account that is attempting to release the funds, determines valid 'receiver'
    account_name_type receiver; ///< the account that should receive funds (might be from, might be to)

    uint32_t escrow_id = 30;
    asset scorum_amount = asset(0, SCORUM_SYMBOL); ///< the amount of scorum to release

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(who);
    }
};

/**
 *  This operation converts SCR into SP at
 *  the current exchange rate. With this operation it is possible to
 *  give another account scorumpower so that faucets can
 *  pre-fund new accounts with scorumpower.
 */
struct transfer_to_scorumpower_operation : public base_operation
{
    account_name_type from;
    account_name_type to; ///< if null, then same as from
    asset amount = asset(0, SCORUM_SYMBOL);

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(from);
    }
};

/**
 * At any given point in time an account can be withdrawing from their
 * scorumpower. A user may change the number of shares they wish to
 * cash out at any time between 0 and their total scorumpower stake.
 *
 * After applying this operation, scorumpower will be withdrawn
 * at a rate of scorumpower/SCORUM_VESTING_WITHDRAW_INTERVALS
 * per week for two years starting
 * one week after this operation is included in the blockchain.
 *
 * This operation is not valid if the user has no scorumpower.
 */
struct withdraw_scorumpower_operation : public base_operation
{
    account_name_type account;
    asset scorumpower = asset(0, SP_SYMBOL);

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(account);
    }
};

/**
 * Allows an account to setup a vesting withdraw but with the additional
 * request for the funds to be transferred directly to another account's
 * balance rather than the withdrawing account. In addition, those funds
 * can be immediately vested again, circumventing the conversion from
 * scorum power to scorum and back, guaranteeing they maintain their value.
 */
struct set_withdraw_scorumpower_route_to_account_operation : public base_operation
{
    account_name_type from_account;
    account_name_type to_account;
    uint16_t percent = 0;
    bool auto_vest = false;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(from_account);
    }
};

struct set_withdraw_scorumpower_route_to_dev_pool_operation : public base_operation
{
    account_name_type from_account;
    uint16_t percent = 0;
    bool auto_vest = false;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(from_account);
    }
};

/**
 *  Users who wish to become a witness must pay a fee acceptable to
 *  the current witnesses to apply for the position and allow voting
 *  to begin.
 *
 *  If the owner isn't a witness he will become a witness.  Witnesses
 *  are charged a fee equal to 1 weeks worth of witness pay which in
 *  turn is derived from the current share supply.  The fee is
 *  only applied if the owner is not already a witness.
 *
 *  If the block_signing_key is null then the witness is removed from
 *  contention.  The network will pick the top 21 witnesses for
 *  producing blocks.
 */
struct witness_update_operation : public base_operation
{
    account_name_type owner;
    std::string url;
    public_key_type block_signing_key;
    chain_properties proposed_chain_props;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(owner);
    }
};

/**
 * All accounts with a VFS can vote for or against any witness.
 *
 * If a proxy is specified then all existing votes are removed.
 */
struct account_witness_vote_operation : public base_operation
{
    account_name_type account;
    account_name_type witness;
    bool approve = true;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(account);
    }
};

struct account_witness_proxy_operation : public base_operation
{
    account_name_type account;
    account_name_type proxy;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(account);
    }
};

/**
 * All account recovery requests come from a listed recovery account. This
 * is secure based on the assumption that only a trusted account should be
 * a recovery account. It is the responsibility of the recovery account to
 * verify the identity of the account holder of the account to recover by
 * whichever means they have agreed upon. The blockchain assumes identity
 * has been verified when this operation is broadcast.
 *
 * This operation creates an account recovery request which the account to
 * recover has 24 hours to respond to before the request expires and is
 * invalidated.
 *
 * There can only be one active recovery request per account at any one time.
 * Pushing this operation for an account to recover when it already has
 * an active request will either update the request to a new new owner authority
 * and extend the request expiration to 24 hours from the current head block
 * time or it will delete the request. To cancel a request, simply set the
 * weight threshold of the new owner authority to 0, making it an open authority.
 *
 * Additionally, the new owner authority must be satisfiable. In other words,
 * the sum of the key weights must be greater than or equal to the weight
 * threshold.
 *
 * This operation only needs to be signed by the the recovery account.
 * The account to recover confirms its identity to the blockchain in
 * the recover account operation.
 */
struct request_account_recovery_operation : public base_operation
{
    account_name_type
        recovery_account; ///< The recovery account is listed as the recovery account on the account to recover.

    account_name_type
        account_to_recover; ///< The account to recover. This is likely due to a compromised owner authority.

    authority new_owner_authority; ///< The new owner authority the account to recover wishes to have. This is secret
    ///< known by the account to recover and will be confirmed in a recover_account_operation

    extensions_type extensions; ///< Extensions. Not currently used.

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(recovery_account);
    }

    void validate() const;
};

/**
 * Recover an account to a new authority using a previous authority and verification
 * of the recovery account as proof of identity. This operation can only succeed
 * if there was a recovery request sent by the account's recover account.
 *
 * In order to recover the account, the account holder must provide proof
 * of past ownership and proof of identity to the recovery account. Being able
 * to satisfy an owner authority that was used in the past 30 days is sufficient
 * to prove past ownership. The get_owner_history function in the database API
 * returns past owner authorities that are valid for account recovery.
 *
 * Proving identity is an off chain contract between the account holder and
 * the recovery account. The recovery request contains a new authority which
 * must be satisfied by the account holder to regain control. The actual process
 * of verifying authority may become complicated, but that is an application
 * level concern, not a blockchain concern.
 *
 * This operation requires both the past and future owner authorities in the
 * operation because neither of them can be derived from the current chain state.
 * The operation must be signed by keys that satisfy both the new owner authority
 * and the recent owner authority. Failing either fails the operation entirely.
 *
 * If a recovery request was made inadvertantly, the account holder should
 * contact the recovery account to have the request deleted.
 *
 * The two setp combination of the account recovery request and recover is
 * safe because the recovery account never has access to secrets of the account
 * to recover. They simply act as an on chain endorsement of off chain identity.
 * In other systems, a fork would be required to enforce such off chain state.
 * Additionally, an account cannot be permanently recovered to the wrong account.
 * While any owner authority from the past 30 days can be used, including a compromised
 * authority, the account can be continually recovered until the recovery account
 * is confident a combination of uncompromised authorities were used to
 * recover the account. The actual process of verifying authority may become
 * complicated, but that is an application level concern, not the blockchain's
 * concern.
 */
struct recover_account_operation : public base_operation
{
    account_name_type account_to_recover; ///< The account to be recovered

    authority new_owner_authority; ///< The new owner authority as specified in the request account recovery operation.

    authority recent_owner_authority; ///< A previous owner authority that the account holder will use to prove past
    /// ownership of the account to be recovered.

    extensions_type extensions; ///< Extensions. Not currently used.

    void get_required_authorities(std::vector<authority>& a) const
    {
        a.push_back(new_owner_authority);
        a.push_back(recent_owner_authority);
    }

    void validate() const;
};

/**
 * Each account lists another account as their recovery account.
 * The recovery account has the ability to create account_recovery_requests
 * for the account to recover. An account can change their recovery account
 * at any time with a 30 day delay. This delay is to prevent
 * an attacker from changing the recovery account to a malicious account
 * during an attack. These 30 days match the 30 days that an
 * owner authority is valid for recovery purposes.
 *
 * On account creation the recovery account is set either to the creator of
 * the account (The account that pays the creation fee and is a signer on the transaction)
 * or to the empty string if the account was mined. An account with no recovery
 * has the top voted witness as a recovery account, at the time the recover
 * request is created. Note: This does mean the effective recovery account
 * of an account with no listed recovery account can change at any time as
 * witness vote weights. The top voted witness is explicitly the most trusted
 * witness according to stake.
 */
struct change_recovery_account_operation : public base_operation
{
    account_name_type account_to_recover; ///< The account that would be recovered in case of compromise
    account_name_type new_recovery_account; ///< The account that creates the recover request
    extensions_type extensions; ///< Extensions. Not currently used.

    void get_required_owner_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(account_to_recover);
    }
    void validate() const;
};

struct decline_voting_rights_operation : public base_operation
{
    account_name_type account;
    bool decline = true;

    void get_required_owner_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(account);
    }
    void validate() const;
};

/**
 * Delegate scorumpower from one account to the other. The scorumpower are still owned
 * by the original account, but content voting rights and bandwidth allocation are transferred
 * to the receiving account. This sets the delegation to `scorumpower`, increasing it or
 * decreasing it as needed. (i.e. a delegation of 0 removes the delegation)
 *
 * When a delegation is removed the shares are placed in limbo for a week to prevent a satoshi
 * of SP from voting on the same content twice.
 */
struct delegate_scorumpower_operation : public base_operation
{
    account_name_type delegator; ///< The account delegating scorumpower
    account_name_type delegatee; ///< The account receiving scorumpower
    asset scorumpower = asset(0, SP_SYMBOL); ///< The amount of scorumpower delegated

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(delegator);
    }
    void validate() const;
};

struct create_budget_operation : public base_operation
{
    budget_type type = budget_type::post;

    account_name_type owner;
    std::string json_metadata;

    asset balance = asset(0, SCORUM_SYMBOL);
    time_point_sec start;
    time_point_sec deadline;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(owner);
    }
};

struct update_budget_operation : public base_operation
{
    budget_type type = budget_type::post;

    int64_t budget_id;
    account_name_type owner;
    std::string json_metadata;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(owner);
    }
};

struct close_budget_operation : public base_operation
{
    budget_type type = budget_type::post;

    int64_t budget_id;
    account_name_type owner;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(owner);
    }
};

struct proposal_create_operation : public base_operation
{
    account_name_type creator;
    uint32_t lifetime_sec = 0;

    proposal_operation operation;

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(creator);
    }

    void validate() const;
};

struct proposal_vote_operation : public base_operation
{
    account_name_type voting_account;
    int64_t proposal_id;

    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(voting_account);
    }

    void validate() const;
};

struct atomicswap_initiate_operation : public base_operation
{
    enum operation_type : bool
    {
        by_initiator = 0,
        by_participant,
    };

    operation_type type = by_initiator;

    account_name_type owner;
    account_name_type recipient;

    asset amount = asset(0, SCORUM_SYMBOL);

    std::string secret_hash;

    std::string metadata;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(owner);
    }
};

struct atomicswap_redeem_operation : public base_operation
{
    account_name_type from;
    account_name_type to; // participant or initiator

    std::string secret;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(to);
    }
};

struct atomicswap_refund_operation : public base_operation
{
    account_name_type participant;
    account_name_type initiator;
    std::string secret_hash;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(participant);
    }
};

struct close_budget_by_advertising_moderator_operation : public base_operation
{
    budget_type type = budget_type::post;

    int64_t budget_id;
    account_name_type moderator;

    void validate() const;
    void get_required_active_authorities(flat_set<account_name_type>& a) const
    {
        a.insert(moderator);
    }
};

} // namespace protocol
} // namespace scorum

// clang-format off

FC_REFLECT( scorum::protocol::account_create_operation,
            (fee)
            (creator)
            (new_account_name)
            (owner)
            (active)
            (posting)
            (memo_key)
            (json_metadata) )

FC_REFLECT( scorum::protocol::account_create_with_delegation_operation,
            (fee)
            (delegation)
            (creator)
            (new_account_name)
            (owner)
            (active)
            (posting)
            (memo_key)
            (json_metadata)
            (extensions) )

FC_REFLECT( scorum::protocol::account_create_by_committee_operation,
            (creator)
            (new_account_name)
            (owner)
            (active)
            (posting)
            (memo_key)
            (json_metadata) )

FC_REFLECT( scorum::protocol::account_update_operation,
            (account)
            (owner)
            (active)
            (posting)
            (memo_key)
            (json_metadata) )

FC_REFLECT( scorum::protocol::transfer_operation, (from)(to)(amount)(memo) )
FC_REFLECT( scorum::protocol::transfer_to_scorumpower_operation, (from)(to)(amount) )
FC_REFLECT( scorum::protocol::withdraw_scorumpower_operation, (account)(scorumpower) )
FC_REFLECT( scorum::protocol::set_withdraw_scorumpower_route_to_account_operation, (from_account)(to_account)(percent)(auto_vest) )
FC_REFLECT( scorum::protocol::set_withdraw_scorumpower_route_to_dev_pool_operation, (from_account)(percent)(auto_vest) )
FC_REFLECT( scorum::protocol::witness_update_operation, (owner)(url)(block_signing_key)(proposed_chain_props) )
FC_REFLECT( scorum::protocol::account_witness_vote_operation, (account)(witness)(approve) )
FC_REFLECT( scorum::protocol::account_witness_proxy_operation, (account)(proxy) )
FC_REFLECT( scorum::protocol::comment_operation, (parent_author)(parent_permlink)(author)(permlink)(title)(body)(json_metadata) )
FC_REFLECT( scorum::protocol::vote_operation, (voter)(author)(permlink)(weight) )

FC_REFLECT( scorum::protocol::delete_comment_operation, (author)(permlink) )
FC_REFLECT( scorum::protocol::comment_options_operation, (author)(permlink)(max_accepted_payout)(allow_votes)(allow_curation_rewards)(extensions) )

FC_REFLECT( scorum::protocol::escrow_transfer_operation, (from)(to)(scorum_amount)(escrow_id)(agent)(fee)(json_meta)(ratification_deadline)(escrow_expiration) )
FC_REFLECT( scorum::protocol::escrow_approve_operation, (from)(to)(agent)(who)(escrow_id)(approve) )
FC_REFLECT( scorum::protocol::escrow_dispute_operation, (from)(to)(agent)(who)(escrow_id) )
FC_REFLECT( scorum::protocol::escrow_release_operation, (from)(to)(agent)(who)(receiver)(escrow_id)(scorum_amount) )
FC_REFLECT( scorum::protocol::prove_authority_operation, (challenged)(require_owner) )
FC_REFLECT( scorum::protocol::request_account_recovery_operation, (recovery_account)(account_to_recover)(new_owner_authority)(extensions) )
FC_REFLECT( scorum::protocol::recover_account_operation, (account_to_recover)(new_owner_authority)(recent_owner_authority)(extensions) )
FC_REFLECT( scorum::protocol::change_recovery_account_operation, (account_to_recover)(new_recovery_account)(extensions) )
FC_REFLECT( scorum::protocol::decline_voting_rights_operation, (account)(decline) )
FC_REFLECT( scorum::protocol::delegate_scorumpower_operation, (delegator)(delegatee)(scorumpower) )

FC_REFLECT( scorum::protocol::create_budget_operation, (type)(owner)(json_metadata)(balance)(start)(deadline) )
FC_REFLECT( scorum::protocol::update_budget_operation, (type)(budget_id)(owner)(json_metadata) )
FC_REFLECT( scorum::protocol::close_budget_operation, (type)(budget_id)(owner) )

FC_REFLECT( scorum::protocol::atomicswap_initiate_operation, (type)(owner)(recipient)(amount)(secret_hash)(metadata) )
FC_REFLECT_ENUM(scorum::protocol::atomicswap_initiate_operation::operation_type,(by_initiator)(by_participant))
FC_REFLECT( scorum::protocol::atomicswap_redeem_operation, (from)(to)(secret) )
FC_REFLECT( scorum::protocol::atomicswap_refund_operation, (participant)(initiator)(secret_hash) )
FC_REFLECT( scorum::protocol::close_budget_by_advertising_moderator_operation, (type)(budget_id)(moderator) )

FC_REFLECT( scorum::protocol::proposal_vote_operation,
            (voting_account)
            (proposal_id))

FC_REFLECT( scorum::protocol::proposal_create_operation,
            (creator)
            (lifetime_sec)
            (operation))
// clang-format on
