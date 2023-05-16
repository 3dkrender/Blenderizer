#include <blender.hpp>

/*
   Listen to WAX reception
   Check memo and sender to evaluate action
      Is a RAM refund?
      Is a deposit to buy RAM?
*/
[[eosio::on_notify("eosio.token::transfer")]] void blender::deposit(name from, name to, asset amount, string memo)
{
   if (from != CONTRACTN && to == CONTRACTN)
   {
      // Does it come from RAM sale?
      if (from == name("eosio.ram"))
      {
         amount.amount = amount.amount * double(RAMFEE);
         // Read users pending RAM refund
         auto itrPendingRam = _pending_ram.begin();
         refund(itrPendingRam->owner, amount, "Ram refund.");
         _pending_ram.erase(itrPendingRam);
      }
      else
      {
         if (from != name("atomicassets"))
         {

            // Is it a deposit? check memo
            check(memo.size() <= 12, "Wrong name Collection!"); // too much text
            name collection = name(memo);

            // Check if collection exists
            atomicassets::collections_t _collections(ATOMIC, ATOMIC.value);
            auto itrCollection = _collections.find(collection.value);
            check(itrCollection != _collections.end(), "Collection not exist!");

            // buy RAM
            buyramproxy(collection, amount);
         } else {
            // Is it backed up? Thanks for your support!
         }
      }
   }
}

/*
   Init RAM refund action called by authorized account in collection
*/
ACTION blender::withdrawram(name authorized_account, name collection, int64_t bytes)
{
   require_auth(authorized_account);

   // Exists collection?
   auto itrCollection = atomicassets::collections.find(collection.value);
   check(itrCollection != atomicassets::collections.end(), "Error 08: Collection not exist!");

   // Is claimer authorized?
   check(isAuthorized(collection, authorized_account), "Error 109: You are not authorized for this operation");

   // Is the collection into blend tables?
   auto itrRamBalance = _rambalance.find(collection.value);
   check(itrRamBalance != _rambalance.end(), "Error 10: The collection is not here!");

   // Does it have enough balance to claim?
   check(itrRamBalance->bytes >= bytes, "Error 11: Not enough RAM to sell!");

   // Add user to waiting list for refunds
   _pending_ram.emplace(_self, [&](auto &rec)
                        {
                           rec.owner = authorized_account;
                           rec.ram_bytes = bytes;
                        });

   // Call to sellram action
   sellram(bytes);

   // Update balances
   if (itrRamBalance->bytes == bytes) // Empty deposit; erase reg.
   {
      _rambalance.erase(itrRamBalance);
   }
   else
   {
      _rambalance.modify(itrRamBalance, _self, [&](auto &rec)
                         { rec.bytes = itrRamBalance->bytes - bytes; });
   }
}

/*
   Create a blend or modify mixture templates
*/
ACTION blender::createblend(name authorized_user, name target_collection, int32_t target_template, vector<int32_t> templates_to_mix)
{
   require_auth(authorized_user);

   // Exists target collection?
   auto itrCollection = atomicassets::collections.require_find(target_collection.value, "Error 12: No collection with this name exists!");

   // Get target collection info
   atomicassets::templates_t _templates = atomicassets::templates_t(ATOMIC, target_collection.value);

   // Does the template exists in collection?
   auto itrTemplates = _templates.require_find(target_template, "Error 13: No template with this id exists!");

   // Is this smart contract authorized in target collection?
   check(isAuthorized(target_collection, CONTRACTN), "Error 14: You must add in your collection this contract account as authorized!");

   // Is this user authorized in this coleecction?
   check(isAuthorized(target_collection, authorized_user), "Error 15: You are not authorized to use this collection!");

   // Create blend info (if exists, update mixture templates)
   auto itrTarget = _blenders.find(target_template);
   if (itrTarget == _blenders.end())
   {
      _blenders.emplace(_self, [&](auto &rec)
                        {
                           rec.owner = authorized_user;
                           rec.collection = target_collection;
                           rec.target = target_template;
                           rec.mixture = templates_to_mix;
                        });
   }
   else
   {
      _blenders.modify(itrTarget, _self, [&](auto &rec)
                       { rec.mixture = templates_to_mix; });
   }
}

/*
   Listen for NFTs arriving to call for a blend
*/
[[eosio::on_notify("atomicassets::transfer")]] void blender::blenderize(name from, name to, vector<uint64_t> asset_ids, string memo)
{
   // ignore NFTs sends by this smart contract. IMPORTANT!
   if (from != CONTRACTN)
   {
      check(memo.size() < 100, "Too much text!");
      uint32_t target = stol(memo);

      // Check if blend exists
      auto itrBlender = _blenders.require_find(target, "Error 02: There is no blender for this target!");

      // Check if this smart contract is authorized
      check(isAuthorized(itrBlender->collection, itrBlender->owner), "Error 03: The collection owner has disavowed this account!!");

      // Get id templates from NFTs received
      vector<int32_t> mainMixtures = itrBlender->mixture;
      vector<int32_t> blendTemplates = {};
      atomicassets::assets_t _assets = atomicassets::assets_t(ATOMIC, get_self().value);
      auto itrAsset = _assets.begin();
      // for (auto it = asset_ids.begin(); it != asset_ids.end(); it++)
      for (size_t i = 0; i < asset_ids.size(); i++)
      {
         // itrAsset = _assets.find(*it);
         itrAsset = _assets.find(asset_ids[i]);
         blendTemplates.push_back(itrAsset->template_id);
      }

      // Check if mixture matches
      sort(blendTemplates.begin(), blendTemplates.end()); // Make sure that vectors are sorted
      sort(mainMixtures.begin(), mainMixtures.end());
      check(blendTemplates == mainMixtures, "Error 05: There is no blender with these mixtures!");

      // Check RAM user balance
      auto itrBalance = _rambalance.require_find(itrBlender->collection.value, "Error 18: Collection not found!");
      check(itrBalance->bytes > 151, "Error 06: There is not enough RAM in this collection!");

      // Check collecton mint limit and supply
      atomicassets::templates_t _templates = atomicassets::templates_t(ATOMIC, itrBlender->collection.value);
      auto itrTemplate = _templates.require_find(itrBlender->target, "Error 17: No template found!");
      check(itrTemplate->max_supply > itrTemplate->issued_supply || itrTemplate->max_supply == 0, "Error 07: This blender cannot mint more assets for that target!");

      // All right; let's blend and burn
      mintasset(itrBlender->collection, itrTemplate->schema_name, itrBlender->target, from);
      burnmixture(asset_ids);

      // Update RAM balance (1 NFT = 151 bytes)
      _rambalance.modify(itrBalance, _self, [&](auto &rec)
                         { rec.bytes = itrBalance->bytes - 151; });
   }
}

/*
   Call to erase blend from tables
*/
ACTION blender::delblend(name authorized_account, int32_t target_template)
{
   require_auth(authorized_account);

   auto itrBlender = _blenders.require_find(target_template, "Error 01: No template with this ID!");

   check(isAuthorized(itrBlender->collection, authorized_account), "Error 16: You are not authorized to delete this blend!");

   _blenders.erase(itrBlender);
}
