#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <atomicassets.hpp>
#include <atomicdata.hpp>

using namespace eosio;
using namespace std;

#define ATOMIC name("atomicassets")
#define EOSIO name("eosio")

/*
   IMPORTANT: Define your smart contract name here
*/
#define CONTRACTN name("your_contract_name")
// -----------------------------------------------

#define RAMFEE 0.995    // Aprox...

CONTRACT blender : public contract
{
public:
   using contract::contract;

   /*
      RAM Actions
   */
   ACTION withdrawram(name authorized_account, name collection, int64_t bytes);

   /*
      Blend Actions
   */
   ACTION createblend(name authorized_user, name target_collection, int32_t target_template, vector<int32_t> templates_to_mix);
   ACTION delblend(name authorized_account, int32_t target_template);

   /*
      System Actions
   */
   [[eosio::on_notify("eosio.token::transfer")]] void deposit(name from, name to, asset amount, string memo);
   [[eosio::on_notify("atomicassets::transfer")]] void blenderize(name from, name to, vector<uint64_t> asset_ids, string memo);

   blender(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds),
                                                                    _rambalance(get_self(), get_self().value),
                                                                    _userres(EOSIO, CONTRACTN.value),
                                                                    _pending_ram(get_self(), get_self().value),
                                                                    _blenders(get_self(), get_self().value),
                                                                    _rammarket(EOSIO, EOSIO.value)
   {
   }

private:
   TABLE rambalance_item
   {
      name collection; // Collection name (Index)
      uint64_t bytes;  // Amount bytes available for this collection

      auto primary_key() const { return collection.value; };
   };
   typedef multi_index<"rambalance"_n, rambalance_item> rambalance_table;

   TABLE blender_item
   {
      name owner;              // Owner account name
      name collection;         // Collection name
      int32_t target;          // Template ID to mint (index)
      vector<int32_t> mixture; // Template IDs <array> to blend

      auto primary_key() const { return target; };
   };
   typedef multi_index<"blenders"_n, blender_item> blender_table;

   // AUX tables to read eosio smart contract tables and get RAM value

   struct userres_item
   {
      name owner;
      asset net_weight;
      asset cpu_weight;
      int64_t ram_bytes;

      auto primary_key() const { return owner.value; };
   };
   typedef multi_index<"userres"_n, userres_item> user_resources;

   struct connector_item
   {
      asset balance;
      double weight;
   };
   typedef connector_item connector;

   struct exchange_state
   {
      asset supply;
      connector base;
      connector quote;
      auto primary_key() const { return supply.amount; };
   };
   typedef multi_index<"rammarket"_n, exchange_state> rammarket;

   TABLE pending_ram_item
   {
      name owner;
      int64_t ram_bytes;
      asset amount;

      auto primary_key() const { return owner.value; };
   };
   typedef multi_index<"pendingram"_n, pending_ram_item> pending_ram_table;

   // Define tables handlers
   rambalance_table _rambalance;
   user_resources _userres;
   pending_ram_table _pending_ram;
   blender_table _blenders;
   rammarket _rammarket;

   // Private Functions

   /*
      Call eosio contract to sell RAM
   */
   void sellram(int64_t bytes)
   {
      action(
          permission_level{CONTRACTN, name("active")},
          "eosio"_n,
          "sellram"_n,
          make_tuple(CONTRACTN, bytes))
          .send();
   }

   /*
      Refund RAM value to collection owner
   */
   void refund(name owner, asset amount, string memo)
   {
      action(
          permission_level{CONTRACTN, name("active")},
          "eosio.token"_n,
          "transfer"_n,
          make_tuple(CONTRACTN, owner, amount, memo))
          .send();
   }

   /*
      Check if user is authorized to mint NFTs
   */
   bool isAuthorized(name collection, name user)
   {
      auto itrCollection = atomicassets::collections.require_find(collection.value, "Error 15: No collection with this name exists!");
      bool authorized = false;
      vector<name> authAccounts = itrCollection->authorized_accounts;
      for (auto it = authAccounts.begin(); it != authAccounts.end() && !authorized; it++)
      {
         if (user == name(*it))
         {
            authorized = true;
         }
      }
      return authorized;
   }

   /*
      Call AtomicAssets contract to mint a new NFT
   */
   void mintasset(name collection, name schema, int32_t template_id, name to)
   {
      vector<uint64_t> returning;
      atomicassets::ATTRIBUTE_MAP nodata = {};
      action(
          permission_level{CONTRACTN, name("active")},
          ATOMIC,
          name("mintasset"),
          make_tuple(CONTRACTN, collection, schema, template_id, to, nodata, nodata, returning))
          .send();
   }

   /*
      Call AtomicAssets contract to burn NFTs
   */
   void burnmixture(vector<uint64_t> mixture)
   {
      for (auto it = mixture.begin(); it != mixture.end(); it++)
      {
         action(
             permission_level{CONTRACTN, name("active")},
             ATOMIC,
             name("burnasset"),
             make_tuple(CONTRACTN, *it))
             .send();
      }
   }

   /*
      Call to eosio contract to buy RAM and update user amount
      (150 bytes every AtomicAsset NFT)
   */
   void buyramproxy(name collection, asset quantity)
   {
      // Call eosio contract to buy RAM
      action(
          permission_level{CONTRACTN, name("active")},
          "eosio"_n,
          "buyram"_n,
          make_tuple(CONTRACTN, CONTRACTN, quantity))
          .send();
            
      // Get RAM price to update balances
      double payRam = quantity.amount * RAMFEE;
      auto itrRamMarket = _rammarket.begin();
      double costRam = itrRamMarket->quote.balance.amount / itrRamMarket->base.balance.amount;
      uint64_t quotaRam = uint64_t(payRam / costRam);
      
      // Update user RAM amount
      auto itrBalance = _rambalance.find(collection.value);
      if (itrBalance == _rambalance.end())
      {
         _rambalance.emplace(_self, [&](auto &rec) {
            rec.collection = collection;
            rec.bytes = quotaRam;
         });
      }
      else
      {
         _rambalance.modify(itrBalance, _self, [&](auto &rec) {
            rec.bytes = itrBalance->bytes + quotaRam;
         });
      }
   }
};