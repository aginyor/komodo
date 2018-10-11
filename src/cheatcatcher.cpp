/********************************************************************
 * (C) 2018 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This supports code to catch nothing at stake cheaters who stake
 * on multiple forks.
 * 
 */

#include "cc/StakeGuard.h"
#include "script/script.h"
#include "main.h"
#include "hash.h"
#include "cheatcatcher.h"
#include "streams.h"

using namespace std;

CCheatList cheatList;
boost::optional<libzcash::SaplingPaymentAddress> cheatCatcher;

uint32_t CCheatList::Prune(uint32_t height)
{
    uint32_t count = 0;
    pair<multimap<const uint32_t, CTxHolder>::iterator, multimap<const uint32_t, CTxHolder>::iterator> range;
    vector<CTxHolder *> toPrune;

    if (height > 0 && NetworkUpgradeActive(height, Params().GetConsensus(), Consensus::UPGRADE_SAPLING))
    {
        LOCK(cs_cheat);
        for (auto it = orderedCheatCandidates.begin(); it != orderedCheatCandidates.end() && it->second.height <= height; it--)
        {
            toPrune.push_back(&it->second);
        }
        count = toPrune.size();
        for (auto ptxHolder : toPrune)
        {
            Remove(*ptxHolder);
        }
    }
    return count;   // return how many removed
}

bool GetStakeParams(const CTransaction &stakeTx, CStakeParams &stakeParams);

bool CCheatList::IsCheatInList(const CTransaction &tx, CTransaction *cheatTx)
{
    // for a tx to be cheat, it needs to spend the same UTXO and be for a different prior block
    // the list should be pruned before this call
    // we return the first valid cheat we find
    CVerusHashWriter hw = CVerusHashWriter(SER_GETHASH, PROTOCOL_VERSION);

    hw << tx.vin[0].prevout.hash;
    hw << tx.vin[0].prevout.n;
    uint256 utxo = hw.GetHash();

    pair<multimap<const uint256, CTxHolder *>::iterator, multimap<const uint256, CTxHolder *>::iterator> range;
    CStakeParams p, s;

    if (GetStakeParams(tx, p))
    {
        LOCK(cs_cheat);
        range = indexedCheatCandidates.equal_range(utxo);
        for (auto it = range.first; it != range.second; it++)
        {
            // we assume height is valid, as we should have pruned the list before checking. since the tx came out of a valid block,
            // what matters is if the prior hash matches
            CTransaction &cTx = it->second->tx;

            // need both parameters to check
            if (GetStakeParams(cTx, s))
            {
                if (p.prevHash != s.prevHash)
                {
                    cheatTx = &cTx;
                    return true;
                }
            }
        }
    }
    return false;
}

bool CCheatList::Add(CTxHolder &txh)
{
    CVerusHashWriter hw = CVerusHashWriter(SER_GETHASH, PROTOCOL_VERSION);
    if (NetworkUpgradeActive(txh.height, Params().GetConsensus(), Consensus::UPGRADE_SAPLING))
    {
        LOCK(cs_cheat);
        auto it = orderedCheatCandidates.insert(pair<const uint32_t, CTxHolder>(txh.height, txh));
        indexedCheatCandidates.insert(pair<const uint256, CTxHolder *>(txh.utxo, &it->second));
    }
}

void CCheatList::Remove(const CTxHolder &txh)
{
    // first narrow by source tx, then compare with tx hash
    uint32_t count;
    pair<multimap<const uint256, CTxHolder *>::iterator, multimap<const uint256, CTxHolder *>::iterator> range;
    vector<multimap<const uint256, CTxHolder *>::iterator> utxoPrune;
    vector<multimap<const int32_t, CTxHolder>::iterator> heightPrune;

    {
        LOCK(cs_cheat);
        range = indexedCheatCandidates.equal_range(txh.utxo);
        for (auto it = range.first; it != range.second; it++)
        {
            uint256 hash = txh.tx.GetHash();
            if (hash == it->second->tx.GetHash())
            {
                utxoPrune.push_back(it);
                auto hrange = orderedCheatCandidates.equal_range(it->second->height);
                for (auto hit = hrange.first; hit != hrange.second; hit++)
                {
                    if (hit->second.tx.GetHash() == hash)
                    {
                        heightPrune.push_back(hit);
                    }
                }
            }
        }
        for (auto it : utxoPrune)
        {
            indexedCheatCandidates.erase(it);
        }
        for (auto it : heightPrune)
        {
            orderedCheatCandidates.erase(it);
        }
    }
}
