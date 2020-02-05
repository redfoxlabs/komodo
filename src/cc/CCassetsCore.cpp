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

/*
 The SetAssetFillamounts() and ValidateAssetRemainder() work in tandem to calculate the vouts for a fill and to validate the vouts, respectively.
 
 This pair of functions are critical to make sure the trading is correct and is the trickiest part of the assets contract.
 
 //vin.0: normal input
 //vin.1: unspendable.(vout.0 from buyoffer) buyTx.vout[0]
 //vin.2+: valid CC output satisfies buyoffer (*tx.vin[2])->nValue
 //vout.0: remaining amount of bid to unspendable
 //vout.1: vin.1 value to signer of vin.2
 //vout.2: vin.2 assetoshis to original pubkey
 //vout.3: CC output for assetoshis change (if any)
 //vout.4: normal output for change (if any)
 //vout.n-1: opreturn [EVAL_ASSETS] ['B'] [assetid] [remaining asset required] [origpubkey]
    ValidateAssetRemainder(remaining_price,tx.vout[0].nValue,nValue,tx.vout[1].nValue,tx.vout[2].nValue,totalunits);
 
 Yes, this is quite confusing...
 
 In ValidateAssetRemainder the naming convention is nValue is the coin/asset with the offer on the books and "units" is what it is being paid in. The high level check is to make sure we didnt lose any coins or assets, the harder to validate is the actual price paid as the "orderbook" is in terms of the combined nValue for the combined totalunits.
 
 We assume that the effective unit cost in the orderbook is valid and that that amount was paid and also that any remainder will be close enough in effective unit cost to not matter. At the edge cases, this will probably be not true and maybe some orders wont be practically fillable when reduced to fractional state. However, the original pubkey that created the offer can always reclaim it.
*/

bool ValidateBidRemainder(int64_t remaining_units, int64_t remaining_nValue, int64_t orig_nValue, int64_t received_nValue, int64_t paid_units, int64_t orig_remaining_units)
{
    int64_t unit_price, received_unit_price, new_unit_price = 0;
    if (orig_nValue == 0 || received_nValue == 0 || paid_units == 0 || orig_remaining_units == 0)
    {
        fprintf(stderr, "%s error any of these values can't be null: orig_nValue == %llu || received_nValue == %llu || paid_units == %llu || vin_remaining_units == %llu\n", __func__, (long long)orig_nValue, (long long)received_nValue, (long long)paid_units, (long long)orig_remaining_units);
        return(false);
    }
    else if (orig_remaining_units != (remaining_units + paid_units))
    {
        fprintf(stderr, "%s error orig_remaining_units %llu != %llu (remaining_units %llu + %llu paid_units)\n", __func__, (long long)orig_remaining_units, (long long)(remaining_units + paid_units), (long long)remaining_units, (long long)paid_units);
        return(false);
    }
    else if (orig_nValue != (remaining_nValue + received_nValue))
    {
        fprintf(stderr, "%s error orig_nValue %llu != %llu (remaining_nValue %llu + %llu received_nValue)\n", __func__, (long long)orig_nValue, (long long)(remaining_nValue - received_nValue), (long long)remaining_nValue, (long long)received_nValue);
        return(false);
    }
    else
    {
        //unitprice = (orig_nValue * COIN) / totalunits;
        //recvunitprice = (received_nValue * COIN) / paidunits;
        //if ( remaining_units != 0 )
        //    newunitprice = (remaining_nValue * COIN) / remaining_units;
        unit_price = (orig_nValue / orig_remaining_units);
        received_unit_price = (received_nValue / paid_units);
        if (remaining_units != 0)
            new_unit_price = (remaining_nValue / remaining_units);   // for debug printing
        if (received_unit_price > unit_price) // can't satisfy bid by sending tokens of higher unit price than requested
        {
            fprintf(stderr, "%s error can't satisfy bid with higher unit price: received_unit_price %.8f > unit_price %.8f, new_unit_price %.8f\n", __func__, (double)received_unit_price / (COIN), (double)unit_price / (COIN), (double)new_unit_price / (COIN));
            CCLogPrintF("ccassets", CCLOG_INFO, "%s error can't satisfy bid with higher unit price: received_unit_price %.8f > unit_price %.8f, new_unit_price %.8f\n", __func__, (double)received_unit_price / (COIN), (double)unit_price / (COIN), (double)new_unit_price / (COIN));
            return(false);
        }
        fprintf(stderr, "%s orig_nValue %llu orig_remaining_units %llu, received_nValue %llu, paid_units %llu, received_unit_price %.8f <= unit_price %.8f, new_unit_price %.8f\n", __func__, (long long)orig_nValue, (long long)orig_remaining_units, (long long)received_nValue, (long long)paid_units, (double)received_unit_price / (COIN), (double)unit_price / (COIN), (double)new_unit_price / (COIN));
    }
    return(true);
}

bool SetBidFillamounts(int64_t &received_nValue, int64_t &remaining_units, int64_t orig_nValue, int64_t &paid_units, int64_t orig_remaining_units)
{
    int64_t remaining_nValue, unit_price; double dprice;

    if (orig_remaining_units == 0)
    {
        received_nValue = remaining_units = paid_units = 0;
        return(false);
    }
    if (paid_units >= orig_remaining_units)
    {
        paid_units = orig_remaining_units;
        received_nValue = orig_nValue;
        remaining_units = 0;
        fprintf(stderr, "%s bid order totally filled!\n", __func__);
        return(true);
    }
    remaining_units = (orig_remaining_units - paid_units);
    //unitprice = (orig_nValue * COIN) / totalunits;
    //received_nValue = (paidunits * unitprice) / COIN;
    unit_price = (orig_nValue / orig_remaining_units);
    received_nValue = (paid_units * unit_price);
    if (unit_price > 0 && received_nValue > 0 && received_nValue <= orig_nValue)
    {
        remaining_nValue = (orig_nValue - received_nValue);
        fprintf(stderr, "%s orig_remaining_units.%llu - paid_units.%llu, remaining_value %llu <- %llu (orig_value.%llu - received_value.%llu)\n", __func__, (long long)orig_remaining_units, (long long)paid_units, (long long)remaining_nValue, (long long)(orig_nValue - received_nValue), (long long)orig_nValue, (long long)received_nValue);
        return (ValidateBidRemainder(remaining_units, remaining_nValue, orig_nValue, received_nValue, paid_units, orig_remaining_units));
    }
    else 
        return(false);
}

bool SetAskFillamounts(int64_t &received_assetoshis, int64_t &remaining_nValue, int64_t orig_assetoshis, int64_t &paid_nValue, int64_t orig_nValue)
{
    int64_t remaining_assetoshis; double dunit_price;
    if (orig_nValue == 0)
    {
        received_assetoshis = remaining_nValue = paid_nValue = 0;
        return(false);
    }
    if (paid_nValue >= orig_nValue)
    {
        paid_nValue = orig_nValue;
        received_assetoshis = orig_assetoshis;
        remaining_nValue = 0;
        fprintf(stderr, "%s ask order totally filled!\n", __func__);
        return(true);
    }
    remaining_nValue = (orig_nValue - paid_nValue);
    dunit_price = ((double)orig_nValue / orig_assetoshis);
    received_assetoshis = (paid_nValue / dunit_price);
    fprintf(stderr, "%s remaining_nValue %.8f (orig_nValue %.8f - paid_nValue %.8f)\n", __func__, (double)remaining_nValue / COIN, (double)orig_nValue / COIN, (double)paid_nValue / COIN);
    fprintf(stderr, "%s orig unit_price %.8f received_assetoshis %llu orig_assetoshis %llu\n", __func__, dunit_price / COIN, (long long)received_assetoshis, (long long)orig_assetoshis);
    if (fabs(dunit_price) > SMALLVAL && received_assetoshis > 0 && received_assetoshis <= orig_assetoshis)
    {
        remaining_assetoshis = (orig_assetoshis - received_assetoshis);
        return(ValidateAskRemainder(remaining_nValue, remaining_assetoshis, orig_assetoshis, received_assetoshis, paid_nValue, orig_nValue));
    }
    else 
        return(false);
}

bool ValidateAskRemainder(int64_t remaining_nValue, int64_t remaining_assetoshis, int64_t orig_assetoshis, int64_t received_assetoshis, int64_t paid_nValue, int64_t orig_nValue)
{
    int64_t unit_price, paid_unit_price, new_unit_price = 0;
    if (orig_assetoshis == 0 || received_assetoshis == 0 || paid_nValue == 0 || orig_nValue == 0)
    {
        fprintf(stderr, "%s error any of these values can't be null: orig_assetoshis == %llu || received_assetoshis == %llu || paid_nValue == %llu || total_nValue == %llu\n", __func__, (long long)orig_assetoshis, (long long)received_assetoshis, (long long)paid_nValue, (long long)orig_nValue);
        return(false);
    }
    else if (orig_nValue != (remaining_nValue + paid_nValue))
    {
        fprintf(stderr, "%s error orig_nValue %llu != %llu (remaining_nValue %llu + paid_nValue %llu \n", __func__, (long long)orig_nValue, (long long)(remaining_nValue + paid_nValue), (long long)remaining_nValue, (long long)paid_nValue);
        return(false);
    }
    else if (orig_assetoshis != (remaining_assetoshis + received_assetoshis))
    {
        fprintf(stderr, "%s error orig_assetoshis %llu != %llu (remaining_nValue %llu + received_nValue %llu)\n", __func__, (long long)orig_assetoshis, (long long)(remaining_assetoshis - received_assetoshis), (long long)remaining_assetoshis, (long long)received_assetoshis);
        return(false);
    }
    else
    {
        unit_price = (orig_nValue / orig_assetoshis);
        paid_unit_price = (paid_nValue / received_assetoshis);
        if (remaining_nValue != 0)
            new_unit_price = (remaining_nValue / remaining_assetoshis);  // for debug printing
        if (paid_unit_price < unit_price)  // can't pay for ask with lower unit price than requested
        {
            fprintf(stderr, "%s error can't pay for ask with lower price: paid_unit_price %.8f < %.8f unit_price, new_unit_price %.8f\n", __func__, (double)paid_unit_price / COIN, (double)unit_price / COIN, (double)new_unit_price / COIN);
            CCLogPrintF("ccassets", CCLOG_INFO, "%s error can't pay for ask with lower price: paid_unit_price %.8f < unit_price %.8f, new_unit_price %.8f\n", __func__, (double)paid_unit_price / COIN, (double)unit_price / COIN, (double)new_unit_price / COIN);

            return(false);
        }
        fprintf(stderr, "%s got paid_unit_price %.8f >= unit_price %.8f, new_unit_price %.8f\n", __func__, (double)paid_unit_price / COIN, (double)unit_price / COIN, (double)new_unit_price / COIN);
    }
    return(true);
}

bool SetSwapFillamounts(int64_t &received_assetoshis, int64_t &remaining_assetoshis2, int64_t orig_assetoshis, int64_t &paid_assetoshis2, int64_t total_assetoshis2)
{
	int64_t remaining_assetoshis; double dunitprice;
	if ( total_assetoshis2 == 0 )
	{
		fprintf(stderr,"%s total_assetoshis2.0 orig_assetoshis.%llu paid_assetoshis2.%llu\n", __func__, (long long)orig_assetoshis,(long long)paid_assetoshis2);
		received_assetoshis = remaining_assetoshis2 = paid_assetoshis2 = 0;
		return(false);
	}
	if ( paid_assetoshis2 >= total_assetoshis2 )
	{
		paid_assetoshis2 = total_assetoshis2;
		received_assetoshis = orig_assetoshis;
		remaining_assetoshis2 = 0;
		fprintf(stderr,"%s swap order totally filled!\n", __func__);
		return(true);
	}
	remaining_assetoshis2 = (total_assetoshis2 - paid_assetoshis2);
	dunitprice = ((double)total_assetoshis2 / orig_assetoshis);
	received_assetoshis = (paid_assetoshis2 / dunitprice);
	fprintf(stderr,"%s remaining_assetoshis2 %llu (%llu - %llu)\n", __func__,(long long)remaining_assetoshis2/COIN,(long long)total_assetoshis2/COIN,(long long)paid_assetoshis2/COIN);
	fprintf(stderr,"%s unitprice %.8f received_assetoshis %llu orig %llu\n", __func__,dunitprice/COIN,(long long)received_assetoshis,(long long)orig_assetoshis);
	if ( fabs(dunitprice) > SMALLVAL && received_assetoshis > 0 && received_assetoshis <= orig_assetoshis )
	{
		remaining_assetoshis = (orig_assetoshis - received_assetoshis);
		return(ValidateAskRemainder(remaining_assetoshis2,remaining_assetoshis,orig_assetoshis,received_assetoshis,paid_assetoshis2,total_assetoshis2));
	} 
    else 
        return(false);
}

bool ValidateSwapRemainder(int64_t remaining_price, int64_t remaining_nValue, int64_t orig_nValue, int64_t received_nValue, int64_t paidunits, int64_t totalunits)
{
    int64_t unitprice, recvunitprice, newunitprice = 0;
    if (orig_nValue == 0 || received_nValue == 0 || paidunits == 0 || totalunits == 0)
    {
        fprintf(stderr, "%s orig_nValue == %llu || received_nValue == %llu || paidunits == %llu || totalunits == %llu\n", __func__, (long long)orig_nValue, (long long)received_nValue, (long long)paidunits, (long long)totalunits);
        return(false);
    }
    else if (totalunits != (remaining_price + paidunits))
    {
        fprintf(stderr, "%s totalunits %llu != %llu (remaining_price %llu + %llu paidunits)\n", __func__, (long long)totalunits, (long long)(remaining_price + paidunits), (long long)remaining_price, (long long)paidunits);
        return(false);
    }
    else if (orig_nValue != (remaining_nValue + received_nValue))
    {
        fprintf(stderr, "%s orig_nValue %llu != %llu (remaining_nValue %llu + %llu received_nValue)\n", __func__, (long long)orig_nValue, (long long)(remaining_nValue - received_nValue), (long long)remaining_nValue, (long long)received_nValue);
        return(false);
    }
    else
    {
        unitprice = (orig_nValue * COIN) / totalunits;
        recvunitprice = (received_nValue * COIN) / paidunits;
        if (remaining_price != 0)
            newunitprice = (remaining_nValue * COIN) / remaining_price;
        if (recvunitprice < unitprice)
        {
            fprintf(stderr, "%s error recvunitprice %.8f < %.8f unitprice, new unitprice %.8f\n", __func__, (double)recvunitprice / (COIN*COIN), (double)unitprice / (COIN*COIN), (double)newunitprice / (COIN*COIN));
            return(false);
        }
        fprintf(stderr, "%s recvunitprice %.8f >= %.8f unitprice, new unitprice %.8f\n", __func__, (double)recvunitprice / (COIN*COIN), (double)unitprice / (COIN*COIN), (double)newunitprice / (COIN*COIN));
    }
    return(true);
}

/* use EncodeTokenCreateOpRet instead:
CScript EncodeAssetCreateOpRet(uint8_t funcid,std::vector<uint8_t> origpubkey,std::string name,std::string description)
{
    CScript opret; uint8_t evalcode = EVAL_ASSETS;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << origpubkey << name << description);
    return(opret);
}
*/

vscript_t EncodeAssetOpRet(uint8_t assetFuncId, uint256 assetid2, int64_t remaining_value, std::vector<uint8_t> origpubkey)
{
    vscript_t vopret; 
	uint8_t evalcode = EVAL_ASSETS;

    switch ( assetFuncId )
    {
        //case 't': this cannot be here
		case 'x': case 'o':
			vopret = /*<< OP_RETURN <<*/ E_MARSHAL(ss << evalcode << assetFuncId);
            break;
        case 's': case 'b': case 'S': case 'B':
            vopret = /*<< OP_RETURN <<*/ E_MARSHAL(ss << evalcode << assetFuncId << remaining_value << origpubkey);
            break;
        case 'E': case 'e':
            assetid2 = revuint256(assetid2);
            vopret = /*<< OP_RETURN <<*/ E_MARSHAL(ss << evalcode << assetFuncId << assetid2 << remaining_value << origpubkey);
            break;
        default:
            fprintf(stderr,"%s illegal funcid.%02x\n", __func__, assetFuncId);
            //opret << OP_RETURN;
            break;
    }
    return(vopret);
}

/* it is for compatibility, do not use this for new contracts (use DecodeTokenCreateOpRet)
bool DecodeAssetCreateOpRet(const CScript &scriptPubKey, std::vector<uint8_t> &origpubkey, std::string &name, std::string &description)
{
    std::vector<uint8_t> vopret; uint8_t evalcode,funcid,*script;
    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( script != 0 && vopret.size() > 2 && script[0] == EVAL_ASSETS && script[1] == 'c' )
    {
        if ( E_UNMARSHAL(vopret,ss >> evalcode; ss >> funcid; ss >> origpubkey; ss >> name; ss >> description) != 0 )
            return(true);
    }
    return(0);
} */

uint8_t DecodeAssetTokenOpRet(const CScript &scriptPubKey, uint8_t &assetsEvalCode, uint256 &tokenid, uint256 &assetid2, int64_t &remaining_units, std::vector<uint8_t> &origpubkey)
{
    vscript_t vopretAssets; //, vopretAssetsStripped;
	uint8_t *script, funcId = 0, assetsFuncId = 0, dummyEvalCode, dummyAssetFuncId;
	uint256 dummyTokenid;
	std::vector<CPubKey> voutPubkeysDummy;
    std::vector<std::pair<uint8_t, vscript_t>>  oprets;

	tokenid = zeroid;
	assetid2 = zeroid;
	remaining_units = 0;
    assetsEvalCode = 0;
    assetsFuncId = 0;

	// First - decode token opret:
	funcId = DecodeTokenOpRet(scriptPubKey, dummyEvalCode, tokenid, voutPubkeysDummy, oprets);
    GetOpretBlob(oprets, OPRETID_ASSETSDATA, vopretAssets);

    LOGSTREAMFN("ccassets", CCLOG_DEBUG2, stream << "from DecodeTokenOpRet returned funcId=" << (int)funcId << std::endl);

	if (funcId == 0 || vopretAssets.size() < 2) {
        LOGSTREAMFN("ccassets", CCLOG_DEBUG1, stream << "incorrect opret or no asset's payload" << " funcId=" << (int)funcId << " vopretAssets.size()=" << vopretAssets.size() << std::endl);
		return (uint8_t)0;
	}

	//if (!E_UNMARSHAL(vopretAssets, { ss >> vopretAssetsStripped; })) {  //strip string size
	//	std::cerr << "DecodeAssetTokenOpRet() could not unmarshal vopretAssetsStripped" << std::endl;
	//	return (uint8_t)0;
	//}

    // additional check to prevent crash
    if (vopretAssets.size() >= 2) {

        assetsEvalCode = vopretAssets.begin()[0];
        assetsFuncId = vopretAssets.begin()[1];

        LOGSTREAMFN("ccassets", CCLOG_DEBUG2, stream << "assetsEvalCode=" << (int)assetsEvalCode <<  " funcId=" << (char)(funcId ? funcId : ' ') << " assetsFuncId=" << (char)(assetsFuncId ? assetsFuncId : ' ') << std::endl);

        if (assetsEvalCode == EVAL_ASSETS)
        {
            //fprintf(stderr,"DecodeAssetTokenOpRet() decode.[%c] assetFuncId.[%c]\n", funcId, assetFuncId);
            switch (assetsFuncId)
            {
            case 'x': case 'o':
                if (vopretAssets.size() == 2)   // no data after 'evalcode assetFuncId' allowed
                {
                    return(assetsFuncId);
                }
                break;
            case 's': case 'b': case 'S': case 'B':
                if (E_UNMARSHAL(vopretAssets, ss >> dummyEvalCode; ss >> dummyAssetFuncId; ss >> remaining_units; ss >> origpubkey) != 0)
                {
                    //fprintf(stderr,"DecodeAssetTokenOpRet() got price %llu\n",(long long)price);
                    return(assetsFuncId);
                }
                break;
            case 'E': case 'e':
                if (E_UNMARSHAL(vopretAssets, ss >> dummyEvalCode; ss >> dummyAssetFuncId; ss >> assetid2; ss >> remaining_units; ss >> origpubkey) != 0)
                {
                    //fprintf(stderr,"DecodeAssetTokenOpRet() got price %llu\n",(long long)price);
                    assetid2 = revuint256(assetid2);
                    return(assetsFuncId);
                }
                break;
            default:
                break;
            }
        }
    }

    LOGSTREAMFN("ccassets", CCLOG_DEBUG1, stream << "no asset's payload or incorrect assets funcId or evalcode" << " funcId=" << (int)funcId << " vopretAssets.size()=" << vopretAssets.size() << " assetsEvalCode=" << assetsEvalCode << " assetsFuncId=" << assetsFuncId << std::endl);
    return (uint8_t)0;
}

// extract sell/buy owner's pubkey from the opret
bool SetAssetOrigpubkey(std::vector<uint8_t> &origpubkey_out, int64_t &remaining_units_out, const CTransaction &tx)
{
    uint256 assetid, assetid2;
    uint8_t evalCode;

    if (tx.vout.size() > 0 && DecodeAssetTokenOpRet(tx.vout[tx.vout.size() - 1].scriptPubKey, evalCode, assetid, assetid2, remaining_units_out, origpubkey_out) != 0)
        return(true);
    else
        return(false);
}

// Calculate seller/buyer's dest cc address from ask/bid tx funcid
bool GetAssetorigaddrs(struct CCcontract_info *cp, char *origCCaddr, char *origNormalAddr, const CTransaction& vintx)
{
    uint256 assetid, assetid2; 
    int64_t price,nValue=0; 
    int32_t n; 
    uint8_t vintxFuncId; 
	std::vector<uint8_t> origpubkey; 
	CScript script;
	uint8_t evalCode;

    n = vintx.vout.size();
    if( n == 0 || (vintxFuncId = DecodeAssetTokenOpRet(vintx.vout[n-1].scriptPubKey, evalCode, assetid, assetid2, price, origpubkey)) == 0 )
        return(false);

	bool bGetCCaddr = false;
    struct CCcontract_info *cpTokens, tokensC;
    cpTokens = CCinit(&tokensC, EVAL_TOKENS);

	if (vintxFuncId == 's' || vintxFuncId == 'S') {
		// bGetCCaddr = GetCCaddress(cpTokens, origCCaddr, pubkey2pk(origpubkey));  
        cpTokens->additionalTokensEvalcode2 = cp->additionalTokensEvalcode2;  // add non-fungible if present
        bGetCCaddr = GetTokensCCaddress(cpTokens, origCCaddr, pubkey2pk(origpubkey));  // tokens to single-eval token or token+nonfungible
	}
	else if (vintxFuncId == 'b' || vintxFuncId == 'B') {
        cpTokens->additionalTokensEvalcode2 = cp->additionalTokensEvalcode2;  // add non-fungible if present
        bGetCCaddr = GetTokensCCaddress(cpTokens, origCCaddr, pubkey2pk(origpubkey));  // tokens to single-eval token or token+nonfungible
	}
	else  {
		LOGSTREAMFN("ccassets", CCLOG_INFO, stream << "incorrect vintx funcid=" << (char)(vintxFuncId?vintxFuncId:' ') << std::endl);
		return false;
	}
    if( bGetCCaddr && Getscriptaddress(origNormalAddr, CScript() << origpubkey << OP_CHECKSIG))
        return(true);
    else 
		return(false);
}


int64_t AssetValidateCCvin(struct CCcontract_info *cp,Eval* eval,char *origCCaddr_out,char *origaddr_out,const CTransaction &tx,int32_t vini,CTransaction &vinTx)
{
	uint256 hashBlock;
    uint256 assetid, assetid2;
    uint256 vinAssetId, vinAssetId2;
	int64_t tmpprice, vinPrice;
    std::vector<uint8_t> tmporigpubkey;
    std::vector<uint8_t> vinOrigpubkey;
    uint8_t evalCode;
    uint8_t vinEvalCode;

	char destaddr[KOMODO_ADDRESS_BUFSIZE], unspendableAddr[KOMODO_ADDRESS_BUFSIZE];

    origaddr_out[0] = destaddr[0] = origCCaddr_out[0] = 0;

    uint8_t funcid = 0;
    uint8_t vinFuncId = 0;
	if (tx.vout.size() > 0) 
		funcid = DecodeAssetTokenOpRet(tx.vout.back().scriptPubKey, evalCode, assetid, assetid2, tmpprice, tmporigpubkey);
    else
        return eval->Invalid("no vouts in tx");

    if( tx.vin.size() < 2 )
        return eval->Invalid("not enough for CC vins");
    else if( tx.vin[vini].prevout.n != 0 )
        return eval->Invalid("vin1 needs to be buyvin.vout[0]");
    else if( eval->GetTxUnconfirmed(tx.vin[vini].prevout.hash, vinTx,hashBlock) == 0 )
    {
		LOGSTREAMFN("ccassets", CCLOG_ERROR, stream << "cannot load vintx for vin=" << vini << " vintx id=" << tx.vin[vini].prevout.hash.GetHex() << std::endl);
        return eval->Invalid("always should find CCvin, but didnt");
    }
//    else if (vinTx.vout.size() < 1 || (vinFuncId = DecodeAssetTokenOpRet(vinTx.vout.back().scriptPubKey, vinEvalCode, vinAssetId, vinAssetId2, vinPrice, vinOrigpubkey)) == 0)
//        return eval->Invalid("could not find assets opreturn in vin tx");
//    else if 
    // check source cc unspendable cc address:
	// if fillSell or cancelSell --> should spend tokens from dual-eval token-assets unspendable addr
    else if((funcid == 'S' || funcid == 'x') && 
		(Getscriptaddress(destaddr, vinTx.vout[tx.vin[vini].prevout.n].scriptPubKey) == 0 || 
		!GetTokensCCaddress(cp, unspendableAddr, GetUnspendable(cp, NULL)) || 
		strcmp(destaddr, unspendableAddr) != 0))
    {
        CCLogPrintF("ccassets", CCLOG_ERROR, "%s cc addr %s is not dual token-evalcode=0x%02x asset unspendable addr %s\n", __func__, destaddr, (int)cp->evalcode, unspendableAddr);
        return eval->Invalid("invalid vin assets CCaddr");
    }
	// if fillBuy or cancelBuy --> should spend coins from asset unspendable addr
	else if ((funcid == 'B' || funcid == 'o') && 
		(Getscriptaddress(destaddr, vinTx.vout[tx.vin[vini].prevout.n].scriptPubKey) == 0 ||
		!GetCCaddress(cp, unspendableAddr, GetUnspendable(cp, NULL)) ||
		strcmp(destaddr, unspendableAddr) != 0))
	{
        CCLogPrintF("ccassets", CCLOG_ERROR, "%s cc addr %s is not evalcode=0x%02x asset unspendable addr %s\n", __func__, destaddr, (int)cp->evalcode, unspendableAddr);
		return eval->Invalid("invalid vin assets CCaddr");
	}
    // end of check source unspendable cc address
    //else if ( vinTx.vout[0].nValue < 10000 )
    //    return eval->Invalid("invalid dust for buyvin");
    // get user dest cc and normal addresses:
    else if(GetAssetorigaddrs(cp, origCCaddr_out, origaddr_out, vinTx) == 0)  
        return eval->Invalid("couldnt get origaddr for buyvin");

    //fprintf(stderr,"AssetValidateCCvin() got %.8f to origaddr.(%s)\n", (double)vinTx.vout[tx.vin[vini].prevout.n].nValue/COIN,origaddr);
    
    if ( vinTx.vout[0].nValue == 0 )
        return eval->Invalid("null value CCvin");

    return(vinTx.vout[0].nValue);
}

int64_t AssetValidateBuyvin(struct CCcontract_info *cp, Eval* eval, int64_t &remaining_units_out, std::vector<uint8_t> &origpubkey_out, char *origCCaddr_out, char *origaddr_out, const CTransaction &tx, uint256 refassetid)
{
    CTransaction vinTx; int64_t nValue; uint256 assetid, assetid2; uint8_t funcid, evalCode;

    origCCaddr_out[0] = origaddr_out[0] = 0;

    // validate locked coins on Assets vin[1]
    if ((nValue = AssetValidateCCvin(cp, eval, origCCaddr_out, origaddr_out, tx, 1, vinTx)) == 0)
        return(0);
    else if (vinTx.vout[0].scriptPubKey.IsPayToCryptoCondition() == 0)
        return eval->Invalid("invalid normal vout0 for buyvin");
    else if ((funcid = DecodeAssetTokenOpRet(vinTx.vout[vinTx.vout.size() - 1].scriptPubKey, evalCode, assetid, assetid2, remaining_units_out, origpubkey_out)) == 'b' &&
        vinTx.vout[1].scriptPubKey.IsPayToCryptoCondition() == 0)  // marker is only in 'b'?
        return eval->Invalid("invalid normal vout1 for buyvin");
    else
    {
        //fprintf(stderr,"have %.8f checking assetid origaddr.(%s)\n",(double)nValue/COIN,origaddr);
        if (vinTx.vout.size() > 0 && funcid != 'b' && funcid != 'B')
            return eval->Invalid("invalid opreturn for buyvin");
        else if (refassetid != assetid)
            return eval->Invalid("invalid assetid for buyvin");
        //int32_t i; for (i=31; i>=0; i--)
        //    fprintf(stderr,"%02x",((uint8_t *)&assetid)[i]);
        //fprintf(stderr," AssetValidateBuyvin assetid for %s\n",origaddr);
    }
    return(nValue);
}

int64_t AssetValidateSellvin(struct CCcontract_info *cp, Eval* eval, int64_t &remaining_units_out, std::vector<uint8_t> &origpubkey_out, char *origCCaddr_out, char *origaddr_out, const CTransaction &tx, uint256 assetid)
{
    CTransaction vinTx; int64_t nValue, assetoshis;
    //fprintf(stderr,"AssetValidateSellvin()\n");
    if ((nValue = AssetValidateCCvin(cp, eval, origCCaddr_out, origaddr_out, tx, 1, vinTx)) == 0)
        return(0);
    if ((assetoshis = IsAssetvout(cp, remaining_units_out, origpubkey_out, vinTx, 0, assetid)) == 0)
        return eval->Invalid("invalid missing CC vout0 for sellvin");
    else
        return(assetoshis);
}


// validates opret for asset tx:
bool ValidateAssetOpret(CTransaction tx, int32_t v, uint256 assetid, int64_t &remaining_units_out, std::vector<uint8_t> &origpubkey_out) {

	uint256 assetidOpret, assetidOpret2;
	uint8_t funcid, evalCode;

	// this is just for log messages indentation fur debugging recursive calls:
	int32_t n = tx.vout.size();

	if ((funcid = DecodeAssetTokenOpRet(tx.vout.back().scriptPubKey, evalCode, assetidOpret, assetidOpret2, remaining_units_out, origpubkey_out)) == 0)
	{
        LOGSTREAMFN("ccassets", CCLOG_DEBUG1, stream << "called DecodeAssetTokenOpRet returned funcId=0 for opret from txid=" << tx.GetHash().GetHex() << std::endl);
		return(false);
	}
/*	it is now on token level:
	else if (funcid == 'c')
	{
		if (assetid != zeroid && assetid == tx.GetHash() && v == 0) {
			//std::cerr  << "ValidateAssetOpret() this is the tokenbase 'c' tx, txid=" << tx.GetHash().GetHex() << " vout=" << v << " returning true" << std::endl;
			return(true);
		}
	}
	else if (funcid == 't')  // TODO: check if this new block does not influence IsAssetVout 
	{
		//std::cerr  << "ValidateAssetOpret() assetid=" << assetid.GetHex() << " assetIdOpret=" << assetidOpret.GetHex() << " txid=" << tx.GetHash().GetHex() << std::endl;
		if (assetid != zeroid && assetid == assetidOpret) {
			//std::cerr << "ValidateAssetOpret() this is a transfer 't' tx, txid=" << tx.GetHash().GetHex() << " vout=" << v << " returning true" << std::endl;
			return(true);
		}
	}   */
	//else if ((funcid == 'b' || funcid == 'B') && v == 0) // critical! 'b'/'B' vout0 is NOT asset
	//	return(false);
	else if (funcid != 'E')
	{
		if (assetid != zeroid && assetidOpret == assetid)
		{
			//std::cerr  << "ValidateAssetOpret() returns true for not 'E', funcid=" << (char)funcid << std::endl;
			return(true);
		}
	}
	else if (funcid == 'E')  // NOTE: not implemented yet!
	{
		if (v < 2 && assetid != zeroid && assetidOpret == assetid)
			return(true);
		else if (v == 2 && assetid != zeroid && assetidOpret2 == assetid)
			return(true);
	}

	//std::cerr  << "ValidateAssetOpret() return false funcid=" << (char)funcid << " assetid=" << assetid.GetHex() << " assetIdOpret=" << assetidOpret.GetHex() << " txid=" << tx.GetHash().GetHex() << std::endl;
	return false;
}  

// Checks if the vout is a really Asset CC vout
int64_t IsAssetvout(struct CCcontract_info *cp, int64_t &remaining_units_out, std::vector<uint8_t> &origpubkey_out, const CTransaction& tx, int32_t v, uint256 refassetid)
{

	//std::cerr  << "IsAssetvout() entered for txid=" << tx.GetHash().GetHex() << " v=" << v << " for assetid=" << refassetid.GetHex() <<  std::endl;

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (v >= n - 1) {  // just moved this up (dimxy)
        LOGSTREAMFN("ccassets", CCLOG_ERROR, stream << "internal err: (v >= n - 1), returning 0" << std::endl);
        return(0);
    }

	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0) // maybe check address too? dimxy: possibly no, because there are too many cases with different addresses here
	{
		// moved opret checking to this new reusable func (dimxy):
		const bool valOpret = ValidateAssetOpret(tx, v, refassetid, remaining_units_out, origpubkey_out);
		//std::cerr << "IsAssetvout() ValidateAssetOpret returned=" << std::boolalpha << valOpret << " for txid=" << tx.GetHash().GetHex() << " for assetid=" << refassetid.GetHex() << std::endl;
		if (valOpret) {
			//std::cerr  << "IsAssetvout() ValidateAssetOpret returned true, returning nValue=" << tx.vout[v].nValue << " for txid=" << tx.GetHash().GetHex() << " for assetid=" << refassetid.GetHex() << std::endl;
			return tx.vout[v].nValue;
		}

		//fprintf(stderr,"IsAssetvout() CC vout v.%d of n=%d amount=%.8f txid=%s\n",v,n,(double)0/COIN, tx.GetHash().GetHex().c_str());
	}
	//fprintf(stderr,"IsAssetvout() normal output v.%d %.8f\n",v,(double)tx.vout[v].nValue/COIN);
	return(0);
} 

// sets cc inputs vs cc outputs and ensures they are equal:
bool AssetCalcAmounts(struct CCcontract_info *cpAssets, int64_t &inputs, int64_t &outputs, Eval* eval, const CTransaction &tx, uint256 assetid)
{
	CTransaction vinTx; uint256 hashBlock, id, id2; int32_t flag; int64_t assetoshis; std::vector<uint8_t> tmporigpubkey; int64_t tmpremainingunits;
	int32_t numvins = tx.vin.size();
	int32_t numvouts = tx.vout.size();
	inputs = outputs = 0;

	struct CCcontract_info *cpTokens, C;

	cpTokens = CCinit(&C, EVAL_TOKENS);

	for (int32_t i = 0; i<numvins; i++)
	{												    // only tokens are relevant!!
		if (/*(*cpAssets->ismyvin)(tx.vin[i].scriptSig)*/ (*cpTokens->ismyvin)(tx.vin[i].scriptSig) ) // || IsVinAllowed(tx.vin[i].scriptSig) != 0)
		{
			//std::cerr << indentStr << "AssetExactAmounts() eval is true=" << (eval != NULL) << " ismyvin=ok for_i=" << i << std::endl;
			// we are not inside the validation code -- dimxy
			if ((eval && eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0) || (!eval && !myGetTransaction(tx.vin[i].prevout.hash, vinTx, hashBlock)))
			{
                LOGSTREAMFN("ccassets", CCLOG_ERROR, stream << "cannot read vintx for i." << i << " vin txid=" << tx.vin[i].prevout.hash.GetHex() << std::endl);
				return (!eval) ? false : eval->Invalid("always should find vin tx, but didnt");
			}
			else {
				// validate vouts of vintx  
				//std::cerr << indentStr << "AssetExactAmounts() check vin i=" << i << " nValue=" << vinTx.vout[tx.vin[i].prevout.n].nValue << std::endl;
				//assetoshis = IsAssetvout(cpAssets, tmpprice, tmporigpubkey, vinTx, tx.vin[i].prevout.n, assetid);
				std::vector<uint8_t> vopretExtra;
				std::vector<CPubKey> vinPubkeysEmpty;

				// TODO: maybe we do not need call to IsTokensVout here, cause we've already selected token vins
				assetoshis = IsTokensvout(false, false, cpTokens, NULL, vinTx, tx.vin[i].prevout.n, assetid);
				if (assetoshis > 0)
				{
					//std::cerr << "AssetCalcAmounts() vin i=" << i << " assetoshis=" << assetoshis << std::endl;
					inputs += assetoshis;
				}
			}
		}
	}

	for (int32_t i = 0; i < numvouts-1; i++) 
	{
		assetoshis = IsAssetvout(cpAssets, tmpremainingunits, tmporigpubkey, tx, i, assetid);
		if (assetoshis != 0)
		{
			//std::cerr << "AssetCalcAmounts() vout i=" << i << " assetoshis=" << assetoshis << std::endl;
			outputs += assetoshis;
		}
	}

	//std::cerr << "AssetCalcAmounts() inputs=" << inputs << " outputs=" << outputs << " for txid=" << tx.GetHash().GetHex() << std::endl;

	/*	we do not verify inputs == outputs here, 
		it's now done in Tokens  */
	return(true);
}
