#include <verso.ntoken/verso.ntoken.hpp>

namespace amax {


void ntoken::create( const name& issuer, const int64_t& maximum_supply, const nsymbol& symbol, const string& token_uri, const name& ipowner ,const name& token_type)
{
   require_auth( issuer );

   check( is_account(issuer), "issuer account does not exist" );
   check( is_account(ipowner) || ipowner.length() == 0, "ipowner account does not exist" );
   check( maximum_supply > 0, "max-supply must be positive" );
   check( token_uri.length() < 1024, "token uri length > 1024" );

   auto nsymb           = symbol;
   auto nstats          = nstats_t::idx_t( _self, _self.value );
   auto idx             = nstats.get_index<"tokenuriidx"_n>();
   auto token_uri_hash  = HASH256(token_uri);
   // auto lower_itr = idx.lower_bound( token_uri_hash );
   // auto upper_itr = idx.upper_bound( token_uri_hash );
   // check( lower_itr == idx.end() || lower_itr == upper_itr, "token with token_uri already exists" );
   check( idx.find(token_uri_hash) == idx.end(), "token with token_uri already exists" );
   check( nstats.find(nsymb.id) == nstats.end(), "token of ID: " + to_string(nsymb.id) + " alreay exists" );
   if (nsymb.id != 0)
      check( nsymb.id != nsymb.parent_id, "parent id shall not be equal to id" );
   else
      nsymb.id         = nstats.available_primary_key();

   nstats.emplace( issuer, [&]( auto& s ) {
      s.supply.symbol   = nsymb;
      s.max_supply      = nasset( maximum_supply, symbol );
      s.token_uri       = token_uri;
      s.ipowner         = ipowner;
      s.issuer          = issuer;
      s.issued_at       = current_time_point();
      s.token_type      = token_type;
   });
}

void ntoken::setnotary(const name& notary, const bool& to_add) {
   require_auth( _self );

   if (to_add)
      _gstate.notaries.insert(notary);

   else
      _gstate.notaries.erase(notary);

}

void ntoken::notarize(const name& notary, const uint32_t& token_id) {
   require_auth( notary );
   check( _gstate.notaries.find(notary) != _gstate.notaries.end(), "not authorized notary" );

   auto nstats = nstats_t::idx_t( _self, _self.value );
   auto itr = nstats.find( token_id );
   check( itr != nstats.end(), "token not found: " + to_string(token_id) );
   nstats.modify( itr, same_payer, [&]( auto& row ) {
      row.notary = notary;
      row.notarized_at = time_point_sec( current_time_point()  );
    });
}

void ntoken::issue( const name& to, const nasset& quantity, const string& memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    auto nstats = nstats_t::idx_t( _self, _self.value );
    auto existing = nstats.find( sym.id );
    check( existing != nstats.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;
    check( to == st.issuer, "tokens can only be issued to issuer account" );

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol mismatch" );
    check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    nstats.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer );
}

void ntoken::retire( const nasset& quantity, const string& memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    auto nstats = nstats_t::idx_t( _self, _self.value );
    auto existing = nstats.find( sym.id );
    check( existing != nstats.end(), "token with symbol does not exist" );
    const auto& st = *existing;

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must retire positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    nstats.modify( st, same_payer, [&]( auto& s ) {
       s.supply -= quantity;
    });

    sub_balance( st.issuer, quantity );
}

void ntoken::transfer( const name& from, const name& to, const vector<nasset>& assets, const string& memo  )
{
   check( from != to, "cannot transfer to self" );
   require_auth( from );
   check( is_account( to ), "to account does not exist");
   check( memo.size() <= 256, "memo has more than 256 bytes" );
   auto payer = has_auth( to ) ? to : from;

   require_recipient( from );
   require_recipient( to );

   for( auto& quantity : assets) {
      auto sym = quantity.symbol;
      auto nstats = nstats_t::idx_t( _self, _self.value );
      const auto& st = nstats.get( sym.id );


      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must transfer positive quantity" );
      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

      sub_balance( from, quantity );
      add_balance( to, quantity, payer );
    }

}


void ntoken::transferfrom( const name& owner, const name& from, const name& to, const vector<nasset>& assets, const string& memo  )
{
   check( from != to, "cannot transfer to self" );
   require_auth( owner );
   check( is_account( to ), "to account does not exist");
   check( memo.size() <= 256, "memo has more than 256 bytes" );
   auto payer = owner;

   allowance_t::idx_t allowances( _self, owner.value);
   auto itr = allowances.find(from.value);
   check( itr != allowances.end(), "Unauthorized");

   require_recipient( owner );
   require_recipient( from );
   require_recipient( to );

   for( auto& nft : assets) {
      auto nstats = nstats_t::idx_t( _self, _self.value );
      const auto& st = nstats.get( nft.symbol.id );
      
      check( nft.is_valid(), "invalid nft" );
      check( nft.amount > 0, "must transfer positive nft amount" );
      check( nft.symbol == st.supply.symbol, "NFT symbol mismatch" );
      check( itr->allowances.count(st.token_type), "Unauthorized NFT:" + st.token_type.to_string() );
      check( itr->allowances.at(st.token_type) >= nft.amount, "Overdrawn nfts" );

      allowances.modify( itr,same_payer, [&](auto& row){
         row.allowances[st.token_type] -= nft.amount;
      });

      sub_balance( from, nft );
      add_balance( to, nft, payer );
    }

}

void ntoken::approve( const name& owner, const name& spender, const name& token_type, const uint64_t& amount ){

   require_auth( owner );

   allowance_t::idx_t allow( _self, owner.value);
   
   auto itr = allow.find( owner.value );

   if ( itr == allow.end()){
      allow.emplace( owner, [&](auto& row){
          row.spender = spender;
          row.allowances[token_type] = amount;
      });
   }else {
       allow.modify( itr,same_payer, [&](auto& row){
          row.allowances[token_type] = amount;
      });
   }

}

void ntoken::sub_balance( const name& owner, const nasset& value ) {
   auto from_acnts = account_t::idx_t( get_self(), owner.value );

   const auto& from = from_acnts.get( value.symbol.raw(), "no balance object found" );
   check( from.balance.amount >= value.amount, "overdrawn balance" );

   from_acnts.modify( from, owner, [&]( auto& a ) {
         a.balance -= value;
      });
}

void ntoken::add_balance( const name& owner, const nasset& value, const name& ram_payer )
{
   auto to_acnts = account_t::idx_t( get_self(), owner.value );
   auto to = to_acnts.find( value.symbol.raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
      });
   } else {
      to_acnts.modify( to, same_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

// void ntoken::open( const name& owner, const symbol& symbol, const name& ram_payer )
// {
//    require_auth( ram_payer );

//    check( is_account( owner ), "owner account does not exist" );

//    auto sym_code_raw = symbol.code().raw();
//    stats statstable( get_self(), sym_code_raw );
//    const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
//    check( st.supply.symbol == symbol, "symbol precision mismatch" );

//    accounts acnts( get_self(), owner.value );
//    auto it = acnts.find( sym_code_raw );
//    if( it == acnts.end() ) {
//       acnts.emplace( ram_payer, [&]( auto& a ){
//         a.balance = asset{0, symbol};
//       });
//    }
// }

// void ntoken::close( const name& owner, const symbol& symbol )
// {
//    require_auth( owner );
//    accounts acnts( get_self(), owner.value );
//    auto it = acnts.find( symbol.code().raw() );
//    check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
//    check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
//    acnts.erase( it );
// }

} //namespace amax