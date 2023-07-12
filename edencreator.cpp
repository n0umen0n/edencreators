#include "edencreator.hpp"
#include <limits>
#include <map>
#include <numeric>
// #include <ranges>
#include <algorithm>
#include <string>

namespace {

// Some compile-time configuration
const vector<name> admins{
    "dan"_n,
};

constexpr int64_t max_supply = static_cast<int64_t>(1'000'000'000e4);

constexpr auto min_groups = size_t{2};
constexpr auto min_group_size = size_t{4};
constexpr auto max_group_size = size_t{6};
// Instead of that declare inside the table
// const auto defaultRewardConfig =
// rewardconfig{.zeos_reward_amt = (int64_t)100e4, .fib_offset = 5};

const auto eleclimit = seconds(7200);

constexpr std::string_view resTransferMemo = "CREATOR distribution";
constexpr std::string_view pollTransferMemo = "$EOS reward";

// Coefficients of 6th order poly where p is phi (ratio between adjacent
// fibonacci numbers) xp^0 + xp^1 ...
constexpr std::array<double, max_group_size> polyCoeffs{
    1, 1.618, 2.617924, 4.235801032, 6.85352607, 11.08900518};

// Other helpers
auto fib(uint8_t index) -> decltype(index) { //
  return (index <= 1) ? index : fib(index - 1) + fib(index - 2);
};

} // namespace

edencreator::edencreator(name self, name code, datastream<const char *> ds)
    : contract(self, code, ds) {}

void edencreator::create(const name &issuer, const asset &maximum_supply) {
  require_auth(_self);

  auto sym = maximum_supply.symbol;
  check(sym.is_valid(), "invalid symbol name");
  check(maximum_supply.is_valid(), "invalid supply");
  check(maximum_supply.amount > 0, "max-supply must be positive");

  stats statstable(_self, sym.code().raw());
  auto existing = statstable.find(sym.code().raw());
  check(existing == statstable.end(), "token with symbol already exists");

  statstable.emplace(_self, [&](auto &s) {
    s.supply.symbol = maximum_supply.symbol;
    s.max_supply = maximum_supply;
    s.issuer = issuer;
  });
}

void edencreator::retire(const asset &quantity, const string &memo) {
  require_auth(get_self());

  validate_symbol(quantity.symbol);
  validate_quantity(quantity);
  validate_memo(memo);

  auto sym = quantity.symbol.code();
  stats statstable(get_self(), sym.raw());
  const auto &st = statstable.get(sym.raw());

  statstable.modify(st, same_payer, [&](auto &s) { s.supply -= quantity; });

  sub_balance(st.issuer, quantity);
}

void edencreator::transfer(const name &from, const name &to,
                           const asset &quantity, const string &memo) {

  if (quantity.symbol == eden_symbol)

  {

    check(from == get_self(), "Can't transfer RESPECT");
  }

  // check(from == get_self(), "we bend the knee to corrupted power");
  require_auth(from);

  validate_symbol(quantity.symbol);
  validate_quantity(quantity);
  validate_memo(memo);

  check(from != to, "cannot transfer to self");
  check(is_account(to), "to account does not exist");

  require_recipient(from);
  require_recipient(to);

  auto payer = has_auth(to) ? to : from;

  sub_balance(from, quantity);
  add_balance(to, quantity, payer);
}

void edencreator::open(const name &owner, const symbol &symbol,
                       const name &ram_payer) {
  require_auth(ram_payer);

  validate_symbol(symbol);

  check(is_account(owner), "owner account does not exist");
  accounts acnts(get_self(), owner.value);
  check(acnts.find(symbol.code().raw()) == acnts.end(),
        "specified owner already holds a balance");

  acnts.emplace(ram_payer, [&](auto &a) { a.balance = asset{0, symbol}; });
}

void edencreator::close(const name &owner, const symbol &symbol) {
  require_auth(owner);

  accounts acnts(get_self(), owner.value);
  auto it = acnts.find(symbol.code().raw());
  check(it != acnts.end(), "Balance row already deleted or never existed. "
                           "Action won't have any effect.");
  check(it->balance.amount == 0,
        "Cannot close because the balance is not zero.");
  acnts.erase(it);
}

void edencreator::rewardamt(const asset &quantity, const uint8_t &offset) {

  require_auth(_self);

  eosrew_t rewtab(_self, _self.value);
  rewardconfgx newrew;

  if (!rewtab.exists()) {
    rewtab.set(newrew, _self);
  } else {
    newrew = rewtab.get();
  }
  newrew.eos_reward_amt = quantity.amount;
  newrew.fib_offset = offset;

  rewtab.set(newrew, _self);
}

void edencreator::startelect() {
  require_admin_auth();

  electinf_t singleton(_self, _self.value);

  electioninfx pede;

  if (!singleton.exists()) {
    singleton.set(pede, _self);
  } else {
    pede = singleton.get();
  }

  check(pede.electionnr !=
            std::numeric_limits<decltype(pede.electionnr)>::max(),
        "election nr overflow");

  // check(false, pede.electionnr);

  pede.starttime = current_time_point();
  pede.electionnr += 1;

  singleton.set(pede, get_self());
}

void edencreator::sub_balance(const name &owner, const asset &value) {
  accounts from_acnts(get_self(), owner.value);

  const auto &from =
      from_acnts.get(value.symbol.code().raw(), "no balance object found");
  check(from.balance.amount >= value.amount, "overdrawn balance");

  from_acnts.modify(from, owner, [&](auto &a) { a.balance -= value; });
}

void edencreator::add_balance(const name &owner, const asset &value,
                              const name &ram_payer) {
  accounts to_acnts(get_self(), owner.value);
  auto to = to_acnts.find(value.symbol.code().raw());
  if (to == to_acnts.end()) {
    to_acnts.emplace(ram_payer, [&](auto &a) { a.balance = value; });
  } else {
    to_acnts.modify(to, same_payer, [&](auto &a) { a.balance += value; });
  }
}

void edencreator::require_admin_auth() {
  bool hasAuth = std::any_of(admins.begin(), admins.end(),
                             [](auto &admin) { return has_auth(admin); });
  check(hasAuth, "missing required authority of admin account");
}

void edencreator::submitcons(const uint64_t &groupnr,
                             const std::vector<name> &rankings,
                             const name &submitter) {

  require_auth(submitter);

  size_t group_size = rankings.size();
  check(group_size >= min_group_size, "too small group");
  check(group_size <= max_group_size, "too big group");

  for (size_t i = 0; i < rankings.size(); i++) {
    std::string rankname = rankings[i].to_string();

    check(is_account(rankings[i]), rankname + " account does not exist.");
  }

  // Getting current election nr
  electinf_t electab(_self, _self.value);
  electioninfx elecitr;

  elecitr = electab.get();

  consensus_t constable(_self, elecitr.electionnr);

  if (constable.find(submitter.value) == constable.end()) {
    constable.emplace(submitter, [&](auto &row) {
      row.rankings = rankings;
      row.submitter = submitter;
      row.groupnr = groupnr;
    });
  } else {
    check(false, "You can vote only once my friend.");
  }
}

void edencreator::issuerez(const name &to, const asset &quantity,
                           const string &memo) {
  action(permission_level{get_self(), "owner"_n}, get_self(), "issue"_n,
         std::make_tuple(to, quantity, memo))
      .send();
};

void edencreator::send(const name &from, const name &to, const asset &quantity,
                       const std::string &memo, const name &contract) {
  action(permission_level{get_self(), "owner"_n}, contract, "transfer"_n,
         std::make_tuple(from, to, quantity, memo))
      .send();
};

void edencreator::validate_symbol(const symbol &symbol) {
  // check(symbol.value == eden_symbol.value, "invalid symbol");
  check(symbol == eden_symbol || symbol == poll_symbol,
        "symbol precision mismatch");
}

void edencreator::validate_quantity(const asset &quantity) {
  check(quantity.is_valid(), "invalid quantity");
  check(quantity.amount > 0, "quantity must be positive");
}

void edencreator::validate_memo(const string &memo) {
  check(memo.size() <= 256, "memo has more than 256 bytes");
}

void edencreator::issue(const name &to, const asset &quantity,
                        const string &memo) {
  // Only able to issue tokens to self
  check(to == get_self(), "tokens can only be issued to issuer account");
  // Only this contract can issue tokens
  require_auth(get_self());
  /*
      check(quantity.symbol.value == eden_symbol.value, "invalid symbol");
      check(quantity.symbol == eden_symbol, "symbol precision mismatch");
  */
  validate_symbol(quantity.symbol);
  validate_quantity(quantity);
  validate_memo(memo);

  auto sym = quantity.symbol.code();
  stats statstable(get_self(), sym.raw());
  const auto &st = statstable.get(sym.raw());
  check(quantity.amount <= st.max_supply.amount - st.supply.amount,
        "quantity exceeds available supply");

  statstable.modify(st, same_payer, [&](auto &s) { s.supply += quantity; });

  add_balance(st.issuer, quantity, st.issuer);
}

void edencreator::submitranks(const AllRankings &ranks) {
  // This action calculates both types of rewards: EOS rewards, and the new
  // token rewards.
  require_auth(get_self());

  eosrew_t rewardConfigTable(_self, _self.value);
  // auto rewardConfig = rewardConfigTable.get_or_default(defaultRewardConfig);
  rewardconfgx newrew;

  newrew = rewardConfigTable.get();

  auto numGroups = ranks.allRankings.size();
  check(numGroups >= min_groups, "Number of groups is too small");

  auto coeffSum =
      std::accumulate(std::begin(polyCoeffs), std::end(polyCoeffs), 0.0);

  // Calculation how much EOS per coefficient.
  auto multiplier = (double)newrew.eos_reward_amt / (numGroups * coeffSum);

  std::vector<int64_t> eosRewards;
  std::transform(std::begin(polyCoeffs), std::end(polyCoeffs),
                 std::back_inserter(eosRewards), [&](const auto &c) {
                   auto finalEosQuant = static_cast<int64_t>(multiplier * c);
                   check(finalEosQuant > 0,
                         "Total configured POLL distribution is too small to "
                         "distibute any reward to rank 1s");
                   return finalEosQuant;
                 });

  std::map<name, uint8_t> accounts;

  for (const auto &rank : ranks.allRankings) {
    size_t group_size = rank.ranking.size();
    check(group_size >= min_group_size, "group size too small");
    check(group_size <= max_group_size, "group size too large");

    auto rankIndex = max_group_size - group_size;
    for (const auto &acc : rank.ranking) {
      check(is_account(acc), "account " + acc.to_string() + " DNE");
      check(0 == accounts[acc]++,
            "account " + acc.to_string() + " listed more than once");

      auto fibAmount = static_cast<int64_t>(fib(rankIndex + newrew.fib_offset));
      auto edenAmt = static_cast<int64_t>(
          fibAmount * std::pow(10, eden_symbol.precision()));
      auto edenQuantity = asset{edenAmt, eden_symbol};

      // TODO: To better scale this contract, any distributions should not use
      // require_recipient.
      //       (Otherwise other user contracts could fail this action)
      // Therefore,
      //   Eden tokens should be added/subbed from balances directly (without
      //   calling transfer) and EOS distribution should be stored, and then
      //   accounts can claim the EOS themselves.

      // issuerez(get_self(), rezpectQuantity, "Mint new REZPECT tokens");

      // Distribute EDEN
      issuerez(get_self(), edenQuantity, "Mint new CREATOR tokens");
      // send(get_self(), acc, rezpectQuantity,
      // rezpectTransferMemo.data(),get_self());
      send(get_self(), acc, edenQuantity, "Distribution of CREATOR tokens",
           get_self());

      // Distribute EOS
      check(eosRewards.size() > rankIndex,
            "Shouldn't happen."); // Indicates that the group is too large, but
                                  // we already check for that?

      auto eosQuantity = asset{eosRewards[rankIndex], poll_symbol};

      send(get_self(), acc, eosQuantity, "Eden Creators $EOS rewards", _self);

      ++rankIndex;
    }
  }
}
