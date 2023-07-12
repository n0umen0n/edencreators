#pragma once

#include <cmath>
// #include <crypto.h>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
// #include <eosiolib/crypto.h>
#include <map>
#include <string>
#include <vector>

using namespace std;
using namespace eosio;

constexpr std::string_view eden_ticker{"CREATOR"};
constexpr symbol poll_symbol{"EOS", 4};
constexpr symbol eden_symbol{eden_ticker, 4};

CONTRACT edencreator : contract {
public:
  struct GroupRanking {
    std::vector<eosio::name> ranking;
  };
  struct AllRankings {
    std::vector<GroupRanking> allRankings;
  };

  TABLE consensus {
    std::vector<eosio::name> rankings;
    uint64_t groupnr;
    eosio::name submitter;

    uint64_t primary_key() const { return submitter.value; }

    uint64_t by_secondary() const { return groupnr; }
  };
  typedef eosio::multi_index<
      "conensus"_n, consensus,
      eosio::indexed_by<
          "bygroupnr"_n,
          eosio::const_mem_fun<consensus, uint64_t, &consensus::by_secondary>>>
      consensus_t;

  TABLE electioninfx {
    uint64_t electionnr = 0;
    eosio::time_point_sec starttime;
  };
  typedef eosio::singleton<"electinfx"_n, electioninfx> electinf_t;

  TABLE rewardconfgx {
    int64_t eos_reward_amt;
    uint8_t fib_offset;
  };

  typedef eosio::singleton<"eosrewx"_n, rewardconfgx> eosrew_t;

  TABLE currency_stats {
    asset supply;
    asset max_supply;
    name issuer;

    uint64_t primary_key() const { return supply.symbol.code().raw(); }
  };

  typedef eosio::multi_index<name("stat"), currency_stats> stats;

  TABLE account {
    asset balance;

    uint64_t primary_key() const { return balance.symbol.code().raw(); }
  };

  typedef eosio::multi_index<name("accounts"), account> accounts;

  edencreator(name self, name code, datastream<const char *> ds);

  ACTION startelect();

  ACTION submitcons(const uint64_t &groupnr, const std::vector<name> &rankings,
                    const name &submitter);

  ACTION rewardamt(const asset &quantity, const uint8_t &offset);

  ACTION transfer(const name &from, const name &to, const asset &quantity,
                  const string &memo);

  // standard eosio.token action to issue tokens
  ACTION issue(const name &to, const asset &quantity, const string &memo);

  ACTION create(const name &issuer, const asset &maximum_supply);

  // distributes rezpect and zeos
  ACTION submitranks(const AllRankings &ranks);

  ACTION retire(const asset &quantity, const string &memo);

  ACTION open(const name &owner, const symbol &symbol, const name &ram_payer);

  ACTION close(const name &owner, const symbol &symbol);

private:
  void validate_symbol(const symbol &symbol);

  void validate_quantity(const asset &quantity);

  void validate_memo(const string &memo);

  void sub_balance(const name &owner, const asset &value);

  void add_balance(const name &owner, const asset &value,
                   const name &ram_payer);

  void send(const name &from, const name &to, const asset &quantity,
            const std::string &memo, const name &contract);

  void issuerez(const name &to, const asset &quantity, const string &memo);

  void require_admin_auth();
};