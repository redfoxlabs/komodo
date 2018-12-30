/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "CCassets.h"
//#include "CCtokens.h"


int64_t AddAssetInputs(struct CCcontract_info *cp,CMutableTransaction &mtx,CPubKey pk,uint256 assetid,int64_t total,int32_t maxinputs)
{
    char coinaddr[64],destaddr[64]; int64_t threshold,nValue,price,totalinputs = 0; uint256 txid,hashBlock; std::vector<uint8_t> origpubkey; CTransaction vintx; int32_t j,vout,n = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    GetCCaddress(cp,coinaddr,pk);
    SetCCunspents(unspentOutputs,coinaddr);
    
	threshold = total/(maxinputs!=0?maxinputs:64); // TODO: is maxinputs really not over 64, what if i want to calc total balance?

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        
		if (it->second.satoshis < threshold)
            continue;

        for (j=0; j<mtx.vin.size(); j++)
            if (txid == mtx.vin[j].prevout.hash && vout == mtx.vin[j].prevout.n)
                break;
        
		if( j != mtx.vin.size() )
            continue;

        if( GetTransaction(txid,vintx,hashBlock,false) != 0 )
        {
            Getscriptaddress(destaddr,vintx.vout[vout].scriptPubKey);
            if( strcmp(destaddr,coinaddr) != 0 && strcmp(destaddr,cp->unspendableCCaddr) != 0 && strcmp(destaddr,cp->unspendableaddr2) != 0 )
                continue;
            fprintf(stderr,"AddAssetInputs() check destaddress=%s vout amount=%.8f\n",destaddr,(double)vintx.vout[vout].nValue/COIN);
            if( (nValue = IsAssetvout(cp, price, origpubkey, vintx, vout, assetid)) > 0 && myIsutxo_spentinmempool(txid,vout) == 0 )
            {
                if ( total != 0 && maxinputs != 0 )
                    mtx.vin.push_back(CTxIn(txid,vout,CScript()));
                nValue = it->second.satoshis;
                totalinputs += nValue;
				//std::cerr << "AddAssetInputs() adding input nValue=" << nValue  << std::endl;
                n++;
                if ( (total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs) )
                    break;
            }
        }
    }

	//std::cerr << "AddAssetInputs() found totalinputs=" << totalinputs << std::endl;
    return(totalinputs);
}


int64_t GetAssetBalance(CPubKey pk,uint256 tokenid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp,C;
    cp = CCinit(&C,EVAL_TOKENS);
    return(AddTokenCCInputs(cp,mtx,pk,tokenid,0,0));
}

UniValue AssetInfo(uint256 assetid)
{
    UniValue result(UniValue::VOBJ); uint256 hashBlock; CTransaction vintx; std::vector<uint8_t> origpubkey; std::string name,description; char str[67],numstr[65];
    if ( GetTransaction(assetid,vintx,hashBlock,false) == 0 )
    {
        fprintf(stderr,"cant find assetid\n");
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","cant find assetid"));
        return(result);
    }
    if ( vintx.vout.size() > 0 && DecodeTokenCreateOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,origpubkey,name,description) == 0 )
    {
        fprintf(stderr,"assetid isnt token creation txid\n");
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","assetid isnt token creation txid"));
    }
    result.push_back(Pair("result","success"));
    result.push_back(Pair("tokenid",uint256_str(str,assetid)));
    result.push_back(Pair("owner",pubkey33_str(str,origpubkey.data())));
    result.push_back(Pair("name",name));
    result.push_back(Pair("supply",vintx.vout[0].nValue));
    result.push_back(Pair("description",description));
    return(result);
}

UniValue AssetList()
{
    UniValue result(UniValue::VARR); 
	std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex; 
	struct CCcontract_info *cp,C; uint256 txid,hashBlock; 
	CTransaction vintx; std::vector<uint8_t> origpubkey; 
	std::string name,description; char str[65];

    cp = CCinit(&C,EVAL_TOKENS);
    SetCCtxids(addressIndex,cp->normaladdr);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++)
    {
        txid = it->first.txhash;
        if ( GetTransaction(txid,vintx,hashBlock,false) != 0 )
        {
            if ( vintx.vout.size() > 0 && DecodeTokenCreateOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,origpubkey,name,description) != 0 )
            {
                result.push_back(uint256_str(str,txid));
            }
        }
    }
    return(result);
}

UniValue AssetOrders(uint256 refassetid)
{
    static uint256 zero;
    int64_t price; 
	uint256 txid,hashBlock,assetid,assetid2; 
	std::vector<uint8_t> origpubkey; 
	CTransaction vintx; 
	UniValue result(UniValue::VARR);  
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputsTokens, unspentOutputsAssets;
	uint8_t funcid, evalCode; 
	char numstr[32],funcidstr[16],origaddr[64],assetidstr[65]; 
	
	struct CCcontract_info *cpTokens, tokensC;
	struct CCcontract_info *cpAssets, assetsC;

    cpTokens = CCinit(&tokensC, EVAL_TOKENS);
	cpAssets = CCinit(&assetsC, EVAL_ASSETS);

    SetCCunspents(unspentOutputsTokens, (char *)cpTokens->unspendableCCaddr);
	SetCCunspents(unspentOutputsAssets, (char *)cpAssets->unspendableCCaddr);


    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputsTokens.begin(); 
		it!=unspentOutputsAssets.end(); 
		// switch iterator to assets unspents:
		(it == unspentOutputsTokens.end()) ? (it = unspentOutputsAssets.begin()) : (++it)  )  // sic!
    {

        txid = it->first.txhash;
        if ( GetTransaction(txid,vintx,hashBlock,false) != 0 ) 
        {
            if (vintx.vout.size() > 0 && (funcid = DecodeAssetOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey, evalCode, assetid, assetid2, price, origpubkey)) != 0)
            {
                if (refassetid != zero && assetid != refassetid)
                {
                    //int32_t z;
                    //for (z=31; z>=0; z--) fprintf(stderr,"%02x",((uint8_t *)&txid)[z]);
                    //fprintf(stderr," txid\n");
                    //for (z=31; z>=0; z--) fprintf(stderr,"%02x",((uint8_t *)&assetid)[z]);
                    //fprintf(stderr," assetid\n");
                    //for (z=31; z>=0; z--) fprintf(stderr,"%02x",((uint8_t *)&refassetid)[z]);
                    //fprintf(stderr," refassetid\n");
                    continue;
                }
                if (vintx.vout[it->first.index].nValue == 0)
                    continue;

                UniValue item(UniValue::VOBJ);

                funcidstr[0] = funcid;
                funcidstr[1] = 0;
                item.push_back(Pair("funcid", funcidstr));
                item.push_back(Pair("txid", uint256_str(assetidstr,txid)));
                item.push_back(Pair("vout", (int64_t)it->first.index));
                if ( funcid == 'b' || funcid == 'B' )
                {
                    sprintf(numstr,"%.8f",(double)vintx.vout[it->first.index].nValue/COIN);
                    item.push_back(Pair("amount",numstr));
                    sprintf(numstr,"%.8f",(double)vintx.vout[0].nValue/COIN);
                    item.push_back(Pair("bidamount",numstr));
                }
                else
                {
                    sprintf(numstr,"%llu",(long long)vintx.vout[it->first.index].nValue);
                    item.push_back(Pair("amount",numstr));
                    sprintf(numstr,"%llu",(long long)vintx.vout[0].nValue);
                    item.push_back(Pair("askamount",numstr));
                }
                if ( origpubkey.size() == 33 )
                {
                    GetCCaddress(cpTokens, origaddr, pubkey2pk(origpubkey));  // TODO: what is this? is it asset or token??
                    item.push_back(Pair("origaddress", origaddr));
                }
                if ( assetid != zeroid )
                    item.push_back(Pair("tokenid",uint256_str(assetidstr,assetid)));
                if ( assetid2 != zeroid )
                    item.push_back(Pair("otherid",uint256_str(assetidstr,assetid2)));
                if ( price > 0 )
                {
                    if ( funcid == 's' || funcid == 'S' || funcid == 'e' || funcid == 'e' )
                    {
                        sprintf(numstr,"%.8f",(double)price / COIN);
                        item.push_back(Pair("totalrequired", numstr));
                        sprintf(numstr,"%.8f",(double)price / (COIN * vintx.vout[0].nValue));
                        item.push_back(Pair("price", numstr));
                    }
                    else
                    {
                        item.push_back(Pair("totalrequired", (int64_t)price));
                        sprintf(numstr,"%.8f",(double)vintx.vout[0].nValue / (price * COIN));
                        item.push_back(Pair("price",numstr));
                    }
                }
                result.push_back(item);
                //fprintf(stderr,"func.(%c) %s/v%d %.8f\n",funcid,uint256_str(assetidstr,txid),(int32_t)it->first.index,(double)vintx.vout[it->first.index].nValue/COIN);
            }
        }
    }
    return(result);
}

// not used (use TokenCreate instead)
/* std::string CreateAsset(int64_t txfee,int64_t assetsupply,std::string name,std::string description)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; struct CCcontract_info *cp,C;
    if ( assetsupply < 0 )
    {
        fprintf(stderr,"negative assetsupply %lld\n",(long long)assetsupply);
        return("");
    }
    cp = CCinit(&C,EVAL_ASSETS);
    if ( name.size() > 32 || description.size() > 4096 )
    {
        fprintf(stderr,"name.%d or description.%d is too big\n",(int32_t)name.size(),(int32_t)description.size());
        return("");
    }
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    if ( AddNormalinputs(mtx,mypk,assetsupply+2*txfee,64) > 0 )
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS,assetsupply,mypk));
        mtx.vout.push_back(CTxOut(txfee,CScript() << ParseHex(cp->CChexstr) << OP_CHECKSIG));
        return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeAssetCreateOpRet('c',Mypubkey(),name,description)));
    }
    return("");
} */
  
// not used (use TokenTransfer instead)
/* std::string AssetTransfer(int64_t txfee,uint256 assetid,std::vector<uint8_t> destpubkey,int64_t total)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; uint64_t mask; int64_t CCchange=0,inputs=0;  struct CCcontract_info *cp,C;
    if ( total < 0 )
    {
        fprintf(stderr,"negative total %lld\n",(long long)total);
        return("");
    }
    cp = CCinit(&C,EVAL_ASSETS);
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    if ( AddNormalinputs(mtx,mypk,txfee,3) > 0 )
    {
        //n = outputs.size();
        //if ( n == amounts.size() )
        //{
        //    for (i=0; i<n; i++)
        //        total += amounts[i];
        mask = ~((1LL << mtx.vin.size()) - 1);
        if ( (inputs= AddAssetInputs(cp,mtx,mypk,assetid,total,60)) > 0 )
        {

			if (inputs < total) {   //added dimxy
				std::cerr << "AssetTransfer(): insufficient funds" << std::endl;
				return ("");
			}
            if ( inputs > total )
                CCchange = (inputs - total);
            //for (i=0; i<n; i++)
            mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS,total,pubkey2pk(destpubkey)));
            if ( CCchange != 0 )
                mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS,CCchange,mypk));
            return(FinalizeCCTx(mask,cp,mtx,mypk,txfee,EncodeAssetOpRet('t',assetid,zeroid,0,Mypubkey())));
        } else fprintf(stderr,"not enough CC asset inputs for %.8f\n",(double)total/COIN);
        //} else fprintf(stderr,"numoutputs.%d != numamounts.%d\n",n,(int32_t)amounts.size());
    }
    return("");
} */

// deprecated
/* std::string AssetConvert(int64_t txfee,uint256 assetid,std::vector<uint8_t> destpubkey,int64_t total,int32_t evalcode)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; int64_t CCchange=0,inputs=0;  struct CCcontract_info *cp,C;
    if ( total < 0 )
    {
        fprintf(stderr,"negative total %lld\n",(long long)total);
        return("");
    }
    cp = CCinit(&C,EVAL_ASSETS);
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    if ( AddNormalinputs(mtx,mypk,txfee,3) > 0 )
    {
        if ( (inputs= AddAssetInputs(cp,mtx,mypk,assetid,total,60)) > 0 )
        {
            if ( inputs > total )
                CCchange = (inputs - total);
            mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS,CCchange,mypk));
            mtx.vout.push_back(MakeCC1vout(evalcode,total,pubkey2pk(destpubkey)));
            return(FinalizeCCTx(0,cp,mtx,mypk,txfee,EncodeAssetOpRet('t',assetid,zeroid,0,Mypubkey())));
        } else fprintf(stderr,"not enough CC asset inputs for %.8f\n",(double)total/COIN);
    }
    return("");
} */

// rpc tokenbid implementation, locks 'bidamount' coins for the 'pricetotal' of tokens
std::string CreateBuyOffer(int64_t txfee, int64_t bidamount, uint256 assetid, int64_t pricetotal)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; 
	struct CCcontract_info *cp, C; 
	uint256 hashBlock; 
	CTransaction vintx; 
	std::vector<uint8_t> origpubkey; 
	std::string name,description;
	int64_t inputs;

    if (bidamount < 0 || pricetotal < 0)
    {
        fprintf(stderr,"negative bidamount %lld, pricetotal %lld\n", (long long)bidamount, (long long)pricetotal);
        return("");
    }
    if (GetTransaction(assetid, vintx, hashBlock, false) == 0)
    {
        fprintf(stderr,"cant find assetid\n");
        return("");
    }
    if (vintx.vout.size() > 0 && DecodeTokenCreateOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey, origpubkey, name, description) == 0)
    {
        fprintf(stderr,"assetid isnt assetcreation txid\n");
        return("");
    }

    cp = CCinit(&C,EVAL_ASSETS);   // NOTE: assets here!
    if (txfee == 0)
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());

    if (inputs= AddNormalinputs(mtx, mypk, bidamount+txfee, 64) > 0)
    {
		if (inputs < bidamount+txfee) {
			std::cerr << "CreateBuyOffer(): insufficient coins to make buy offer" << std::endl;
			CCerror = strprintf("insufficient coins to make buy offer");
			return ("");
		}

        mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, bidamount, GetUnspendable(cp,0)));
        return(FinalizeCCTx(0, cp, mtx, mypk, txfee, EncodeAssetOpRet('b', assetid, zeroid, pricetotal, Mypubkey())));
    }
    return("");
}

// rpc tokenask implementation, locks 'askamount' tokens for the 'pricetotal' 
std::string CreateSell(int64_t txfee,int64_t askamount,uint256 assetid,int64_t pricetotal)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; 
	uint64_t mask; 
	int64_t inputs, CCchange; 
	CScript opret; 
	struct CCcontract_info *cp,C;

	//std::cerr << "CreateSell() askamount=" << askamount << " pricetotal=" << pricetotal << std::endl;

    if (askamount < 0 || pricetotal < 0)    {
        fprintf(stderr,"negative askamount %lld, askamount %lld\n",(long long)pricetotal,(long long)askamount);
        return("");
    }

    cp = CCinit(&C, EVAL_TOKENS);  // NOTE: tokens is here

    if ( txfee == 0 )
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        mask = ~((1LL << mtx.vin.size()) - 1);
        if ((inputs = AddTokenCCInputs(cp, mtx, mypk, assetid, askamount, 60)) > 0)
        {
			if (inputs < askamount) {
				//was: askamount = inputs;
				std::cerr << "CreateSell(): insufficient tokens for ask" << std::endl;
				CCerror = strprintf("insufficient tokens for ask");
				return ("");
			}

            mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS,askamount, GetUnspendable(cp,0)));
            if (inputs > askamount)
                CCchange = (inputs - askamount);
            if (CCchange != 0)
                mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, CCchange, mypk));

            opret = EncodeAssetOpRet('s',assetid, zeroid, pricetotal, Mypubkey());
            return(FinalizeCCTx(mask,cp,mtx,mypk,txfee,opret));
		}
		else {
			fprintf(stderr, "need some tokens to place ask\n");
		}
    }
	else {  // dimxy added 'else', because it was misleading message before
		fprintf(stderr, "need some native coins to place ask\n");
	}
    return("");
}

std::string CreateSwap(int64_t txfee,int64_t askamount,uint256 assetid,uint256 assetid2,int64_t pricetotal)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; uint64_t mask; int64_t inputs,CCchange; CScript opret; struct CCcontract_info *cp,C;

	//////////////////////////////////////////
    fprintf(stderr,"asset swaps disabled\n");
    return("");
	//////////////////////////////////////////

    if ( askamount < 0 || pricetotal < 0 )
    {
        fprintf(stderr,"negative askamount %lld, askamount %lld\n",(long long)pricetotal,(long long)askamount);
        return("");
    }
    cp = CCinit(&C, EVAL_ASSETS);

    if ( txfee == 0 )
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        mask = ~((1LL << mtx.vin.size()) - 1);
        if ((inputs = AddAssetInputs(cp, mtx, mypk, assetid, askamount, 60)) > 0)
        {
			if (inputs < askamount) {
				//was: askamount = inputs;
				std::cerr << "CreateSwap(): insufficient tokens for ask" << std::endl;
				CCerror = strprintf("insufficient tokens for ask");
				return ("");
			}

            mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, askamount, GetUnspendable(cp, 0)));			

            if (inputs > askamount)
                CCchange = (inputs - askamount);
            if (CCchange != 0)
                mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, CCchange, mypk));

            if (assetid2 == zeroid)
                opret = EncodeAssetOpRet('s', assetid, zeroid, pricetotal, Mypubkey());
            else
            {
                opret = EncodeAssetOpRet('e', assetid, assetid2, pricetotal, Mypubkey());
            }
            return(FinalizeCCTx(mask,cp,mtx,mypk,txfee,opret));
        } 
		else {
			fprintf(stderr, "need some assets to place ask\n");
		}
    }
	else { // dimxy added 'else', because it was misleading message before
		fprintf(stderr,"need some native coins to place ask\n");
	}
    
    return("");
}

// unlocks coins
std::string CancelBuyOffer(int64_t txfee,uint256 assetid,uint256 bidtxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint64_t mask; 
	uint256 hashBlock; 
	int64_t bidamount; 
	CPubKey mypk; 
	struct CCcontract_info *cp,C;

    cp = CCinit(&C, EVAL_ASSETS);

    if ( txfee == 0 )
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        mask = ~((1LL << mtx.vin.size()) - 1);
        if (GetTransaction(bidtxid, vintx, hashBlock, false) != 0)
        {
            bidamount = vintx.vout[0].nValue;
            mtx.vin.push_back(CTxIn(bidtxid, 0, CScript()));
            mtx.vout.push_back(CTxOut(bidamount,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
            return(FinalizeCCTx(mask, cp, mtx, mypk, txfee, EncodeAssetOpRet('o', assetid, zeroid, 0, Mypubkey())));
        }
    }
    return("");
}

//unlocks tokens
std::string CancelSell(int64_t txfee,uint256 assetid,uint256 asktxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; uint64_t mask; uint256 hashBlock; int64_t askamount; CPubKey mypk; 
	struct CCcontract_info *cp,C;

    cp = CCinit(&C, EVAL_TOKENS);

    if ( txfee == 0 )
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        mask = ~((1LL << mtx.vin.size()) - 1);
        if (GetTransaction(asktxid, vintx, hashBlock, false) != 0)
        {
            askamount = vintx.vout[0].nValue;
            mtx.vin.push_back(CTxIn(asktxid, 0, CScript()));
            mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, askamount, mypk));
            return(FinalizeCCTx(mask, cp, mtx, mypk, txfee, EncodeAssetOpRet('x', assetid, zeroid, 0, Mypubkey())));
        }
    }
    return("");
}

//send tokens, receive coins:
std::string FillBuyOffer(int64_t txfee,uint256 assetid,uint256 bidtxid,int64_t fillamount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 
	CPubKey mypk; 
	std::vector<uint8_t> origpubkey; 
	int32_t bidvout=0; 
	uint64_t mask; 
	int64_t origprice, bidamount, paid_amount, remaining_required, inputs, CCchange=0; 
	struct CCcontract_info *cp,C;

    if (fillamount < 0)
    {
        fprintf(stderr,"negative fillamount %lld\n", (long long)fillamount);
        return("");
    }
    cp = CCinit(&C, EVAL_TOKENS);
    
	if ( txfee == 0 )
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        mask = ~((1LL << mtx.vin.size()) - 1);
        if (GetTransaction(bidtxid, vintx, hashBlock, false) != 0)
        {
            bidamount = vintx.vout[bidvout].nValue;
            SetAssetOrigpubkey(origpubkey, origprice, vintx);
          
			mtx.vin.push_back(CTxIn(bidtxid, bidvout, CScript()));

            if ((inputs = AddTokenCCInputs(cp, mtx, mypk, assetid, fillamount, 60)) > 0)
            {
				if (inputs < fillamount) {
					std::cerr << "FillBuyOffer(): insufficient tokens to fill buy offer" << std::endl;
					CCerror = strprintf("insufficient tokens to fill buy offer");
					return ("");
				}
                
				SetBidFillamounts(paid_amount, remaining_required, bidamount, fillamount, origprice);
                
				if (inputs > fillamount)
                    CCchange = (inputs - fillamount);
                
				mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, bidamount - paid_amount, GetUnspendable(cp,0)));    // tokens
                mtx.vout.push_back(CTxOut(paid_amount,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
                mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, fillamount, pubkey2pk(origpubkey)));				// coins
                
				if (CCchange != 0)
                    mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, CCchange, mypk));								// coins

                fprintf(stderr,"remaining %llu -> origpubkey\n", (long long)remaining_required);

                return(FinalizeCCTx(mask,cp,mtx,mypk,txfee, EncodeAssetOpRet('B', assetid, zeroid, remaining_required, origpubkey)));
            } else return("dont have any assets to fill bid\n");
        }
    }
    return("no normal coins left");
}


// send coins, receive tokens 
std::string FillSell(int64_t txfee,uint256 assetid,uint256 assetid2,uint256 asktxid,int64_t fillunits)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx,filltx; 
	uint256 hashBlock; 
	CPubKey mypk; 
	std::vector<uint8_t> origpubkey; 
	double dprice; 
	uint64_t mask; 
	int32_t askvout=0; 
	int64_t received_assetoshis, total_nValue, orig_assetoshis, paid_nValue, remaining_nValue, inputs, CCchange=0; 
	struct CCcontract_info *cpTokens, tokensC;
	struct CCcontract_info *cpAssets, assetsC;

    if (fillunits < 0)
    {
        CCerror = strprintf("negative fillunits %lld\n",(long long)fillunits);
        fprintf(stderr,"%s\n",CCerror.c_str());
        return("");
    }
    if (assetid2 != zeroid)
    {
        CCerror = "asset swaps disabled";
        fprintf(stderr,"%s\n",CCerror.c_str());
        return("");
    }

    cpTokens = CCinit(&tokensC, EVAL_TOKENS);
	cpAssets = CCinit(&assetsC, EVAL_ASSETS);

    if (txfee == 0)
        txfee = 10000;

    mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx,mypk,txfee,3) > 0)
    {
        mask = ~((1LL << mtx.vin.size()) - 1);
        if (GetTransaction(asktxid, vintx, hashBlock, false) != 0)
        {
            orig_assetoshis = vintx.vout[askvout].nValue;
            SetAssetOrigpubkey(origpubkey, total_nValue, vintx);
            dprice = (double)total_nValue / orig_assetoshis;
            paid_nValue = dprice * fillunits;

            mtx.vin.push_back(CTxIn(asktxid, askvout, CScript()));		// NOTE: this is the reference to tokens -> send cpTokens for signing into FinalizeCCTx!

            if (assetid2 != zeroid)
                inputs = AddAssetInputs(cpTokens, mtx, mypk, assetid2, paid_nValue, 60);
            else
            {
                inputs = AddNormalinputs(mtx, mypk, paid_nValue, 60);
                mask = ~((1LL << mtx.vin.size()) - 1);
            }
            if (inputs > 0)
            {
				if (inputs < paid_nValue) {
					std::cerr << "FillSell(): insufficient coins to fill sell" << std::endl;
					CCerror = strprintf("insufficient coins to fill sell");
					return ("");
				}
                
				if (assetid2 != zeroid)
                    SetSwapFillamounts(received_assetoshis, remaining_nValue, orig_assetoshis, paid_nValue, total_nValue);
                else 
					SetAskFillamounts(received_assetoshis, remaining_nValue, orig_assetoshis, paid_nValue, total_nValue);

                if (assetid2 != zeroid && inputs > paid_nValue)
                    CCchange = (inputs - paid_nValue);

                mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, orig_assetoshis - received_assetoshis, GetUnspendable(cpAssets, NULL)));   // coins
                mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, received_assetoshis, mypk));					// tokens
                
				if (assetid2 != zeroid)
                    mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, paid_nValue, origpubkey));					// tokens (not implemented correctly)
                else 
					mtx.vout.push_back(CTxOut(paid_nValue, CScript() << origpubkey << OP_CHECKSIG));		// coins
                
				if (CCchange != 0)
                    mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, CCchange, mypk));							// coins

                return(FinalizeCCTx(mask,cpTokens,mtx,mypk,txfee,EncodeAssetOpRet(assetid2 != zeroid ? 'E' : 'S', assetid, assetid2, remaining_nValue, origpubkey)));
            } else {
                CCerror = strprintf("filltx not enough utxos");
                fprintf(stderr,"%s\n", CCerror.c_str());
            }
        }
    }
    return("");
}
