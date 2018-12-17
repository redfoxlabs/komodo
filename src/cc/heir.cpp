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

#include "CCHeir.h"
#include "CCassets.h"

#include "heir_validate.h"

class CoinHelper;
class TokenHelper;

/*
 The idea of Heir CC is to allow crypto inheritance.
 A special 1of2 CC address is created that is freely spendable by the creator. The heir is only allowed to spend after the specified amount of idle blocks. The idea is that if the address doesnt spend any funds for a year (or whatever amount set), then it is time to allow the heir to spend. The design requires the heir to spend all the funds at once
*/

// tx validation code

// this is for indentation of debug log messages (in recursive calls):
extern thread_local uint32_t assetValIndentSize;

// check if vout is cc addr and also check sum(inputs) == sum(outputs) for the passed tx, if requested
int64_t IsHeirvout(bool compareTotals, struct CCcontract_info *cpHeir, Eval* eval, uint256 assetid, const CTransaction& tx, int32_t v)
{
	std::string indentStr = std::string().append(assetValIndentSize, '.');

	//std::cerr << indentStr << "IsHeirvout() entered for txid=" << tx.GetHash().GetHex() << " v=" << v << std::boolalpha << " compareTotals=" << compareTotals  << std::endl;
	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition())
	{
		//std::cerr << indentStr << "IsHeirvout() IsPayToCryptoCondition=true for txid=" << tx.GetHash().GetHex() << std::endl;
		if (compareTotals) {  // totally there are only 2 levels actually

			// call recursively HeirExactTokenAmounts and compare ccinputs = ccoutputs for this tx:
			assetValIndentSize++;
			const bool isEqual = HeirExactTokenAmounts(false, cpHeir, eval, assetid, tx);
			assetValIndentSize--;

			if (!isEqual) {  // ccInputs != ccOutputs means a problem
				//std::cerr << indentStr << "IsHeirvout() warning: detected suspicious tx=" << tx.GetHash().GetHex() << ": cc inputs != cc outputs, checking further if it is the tokenbase tx" << std::endl;
				// if ccInputs != ccOutputs and it is not the 'tokenbase' tx means it is possibly fake tx (dimxy):
				if (assetid != zeroid && assetid != tx.GetHash()) {
					std::cerr << indentStr << "IsHeirvout() warning: detected bad tx=" << tx.GetHash().GetHex() << ": cc inputs != cc outputs and not the 'tokenbase' tx, skipping this tx" << std::endl;
					return 0;
				}
			}
		}

		// lets check asset opreturn for this heir or assets tx (dimxy):
		/*
		int64_t dummyPrice; std::vector<uint8_t> dummyOrigpubkey;

		const bool valOpret = ValidateAssetOpret(tx, v, assetid, dummyPrice, dummyOrigpubkey);
		//std::cerr << indentStr << "IsHeirvout() ValidateAssetOpret returned=" << std::boolalpha << valOpret << " for txid=" << tx.GetHash().GetHex() << std::endl;
		if (valOpret) {
			std::cerr << indentStr << "IsHeirvout() opret is true, return value=" << tx.vout[v].nValue << " for txid=" << tx.GetHash().GetHex() << std::endl;
			return(tx.vout[v].nValue);
		}*/

		return(tx.vout[v].nValue);
	}
	//std::cerr << indentStr << "IsHeirvout() return value=0" << std::endl;
	return(0);
}

// this function validates that tx cc outputs == cc inputs,
// that is there is no fake token supply from normal inputs (except the initial tokenbase tx)
// the cc inputs are allowed only from the Assets or Heir contracts
bool HeirExactTokenAmounts(bool compareTotals, struct CCcontract_info *cpHeir, Eval* eval, uint256 assetid, const CTransaction &tx)
{
	static uint256 zerohash;
	CTransaction vinTx;
	uint256 hashBlock, activehash;
	int64_t inputs = 0, outputs = 0, assetoshis;

	struct CCcontract_info *cpAssets, cAssets;
	cpAssets = CCinit(&cAssets, EVAL_ASSETS);		// init also tokens CC contract to check its cc addresses too

	std::string indentStr = std::string().append(assetValIndentSize, '.');

	int32_t numvins = tx.vin.size();
	int32_t numvouts = tx.vout.size();
	for (int32_t i = 0; i < numvins; i++)
	{
		//std::cerr << indentStr << "HeirExactTokenAmounts() vin i=" << i << " cpHeir->ismyvin()=" << std::boolalpha << (*cpHeir->ismyvin)(tx.vin[i].scriptSig) << " cpAssets->ismyvin()=" << (*cpAssets->ismyvin)(tx.vin[i].scriptSig) << std::endl;

		// checking that vin is either from heir or assets:
		if ((*cpHeir->ismyvin)(tx.vin[i].scriptSig) || (*cpAssets->ismyvin)(tx.vin[i].scriptSig))
		{
			//std::cerr << indentStr; fprintf(stderr,"vini.%d check mempool\n",i);
			if ((eval && !eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock)) || !myGetTransaction(tx.vin[i].prevout.hash, vinTx, hashBlock)) {
				std::cerr << indentStr << "HeirExactTokenAmounts(): can't get vintx transaction txid=" << tx.vin[i].prevout.hash.GetHex() << std::endl;
				return (eval) ? eval->Invalid("cant find vinTx") : false;
			}
			else
			{
				//std::cerr << indentStr; fprintf(stderr,"vini.%d check hash and vout\n",i);
				if (hashBlock == zerohash) {
					std::cerr << indentStr << "HeirExactTokenAmounts(): can't get vintx from mempool, txid=" << tx.vin[i].prevout.hash.GetHex() << std::endl;
					return (eval) ? eval->Invalid("cant Heir from mempool") : false;
				}

				std::string dummyRefcoin; uint256 dummyBindtxid, dummyDeposittxid; CPubKey dummyDestpub; int64_t dummyAmount;
				uint256 dummyAssetid2;
				std::vector<uint8_t> dummyOrigpubkey;

				// Note: if assetid is zeroid, it may mean we are on the first level and just called from HeirValidate, validating claim 't' tx, 
				// then let's find the assetid ourselves:
				if (assetid == zeroid && DecodeAssetOpRet(tx.vout[numvouts - 1].scriptPubKey, assetid, dummyAssetid2, dummyAmount, dummyOrigpubkey) == 't') {
					//std::cerr << indentStr << "HeirExactTokenAmounts() will check if this vinx is the tokenbase assetid=" << assetid.GetHex() << std::endl;
				}

				// checking that the vout of the vintx (that is, referenced by this vin), in its turn, is fed by either from heir' or assets' cryptocondition address:
				//std::cerr << indentStr << "HeirExactTokenAmounts() calling IsHeirvout for vintx i=" << i << " prevout.n=" << tx.vin[i].prevout.n << std::endl;

				assetValIndentSize++;
				assetoshis = IsHeirvout(compareTotals, cpHeir, eval, assetid, vinTx, tx.vin[i].prevout.n);
				assetValIndentSize--;
				if (assetoshis > 0)
					inputs += assetoshis;
			}
		}
	}
	for (int32_t i = 0; i<numvouts; i++)
	{
		//fprintf(stderr,"i.%d of numvouts.%d\n",i,numvouts);

		//std::cerr << indentStr << "HeirExactTokenAmounts() calling IsHeirvout for this tx, vout i=" << i << std::endl;
		assetValIndentSize++;
		// Note: we pass in here compareTotals = false because we don't need to call HeirExactAmounts() recursively from isHeirvout
		// indeed, in this case we'll be checking this tx again
		assetoshis = IsHeirvout(false, cpHeir, eval, assetid, tx, i);
		assetValIndentSize--;
		if (assetoshis > 0)
			outputs += assetoshis;
	}

	if (inputs != outputs) {
		std::cerr << indentStr << "HeirExactTokenAmounts() inputs=" << inputs << " vs outputs=" << outputs << " return false" << std::endl;
		return (eval) ? eval->Invalid("mismatched inputs != outputs + txfee") : false;
	}
	else {
		//std::cerr << indentStr << "HeirExactTokenAmounts() inputs=" << inputs << " vs outputs=" << outputs << " return true" << std::endl;
		return(true);
	}
}


// claim coins tokens validation runner
// sadly we cannot have yet 'templatized' lambdas, if we could we could capture all these params inside HeirValidation()
template <typename Helper> bool RunValidationPlans(uint8_t funcId, struct CCcontract_info* cp, Eval* eval, const CTransaction& tx, CScript opRetScript, uint256 latestTxid, CScript latestTxOpRetScript, bool isHeirSpendingBegan)
{
	int32_t numvins = tx.vin.size();
	int32_t numvouts = tx.vout.size();

	// setup validation framework (please see its description in heir_validate.h):
	// validation 'plans':
	CInputValidationPlan<CValidatorBase> vinPlan;
	COutputValidationPlan<CValidatorBase> voutPlan;

	// vin 'identifiers'	
	CNormalInputIdentifier normalInputIdentifier(cp);
	CCCInputIdentifier ccInputIdentifier(cp);

	// vin and vout 'validators'
	// always check coin inputs:
	CMyPubkeyVoutValidator<Helper>	normalInputValidator(cp, opRetScript, true);									// check normal input for this opret cause this is first tx
	CMyPubkeyVoutValidator<Helper>	normalInputValidatorLast(cp, latestTxOpRetScript, true);						// check normal input for latest opret. TODO: we may also check this opret

	CCC1of2AddressValidator<Helper> cc1of2ValidatorThis(cp, opRetScript, "checking this tx opreturn:");				// 1of2add validator with pubkeys from this tx opreturn
	CCC1of2AddressValidator<Helper> cc1of2ValidatorLast(cp, latestTxOpRetScript, "checking last tx opreturn:");     // 1of2add validator with pubkeys from last tx opreturn
	CHeirSpendValidator<Helper>	    heirSpendValidator(cp, latestTxOpRetScript, latestTxid, isHeirSpendingBegan);	// check if heir allowed to spend

	// only for tokens:
	CMyPubkeyVoutValidator<TokenHelper>	ownerCCaddrValidator(cp, opRetScript, false);								// check if this correct owner's cc user addr corresponding to opret
	CMyPubkeyVoutValidator<TokenHelper>	ownerCCaddrValidatorLast(cp, latestTxOpRetScript, false);					// check if this correct owner's cc user addr corresponding to lastest opret 
																													// TODO: we may also check with current opret
	COpRetValidator<Helper>		    opRetValidator(cp, latestTxOpRetScript);										// compare opRets in this and last tx

	switch (funcId) {
	case 'G': // fund tokens
		// vin validation plan:
		//											we need cast here							this is casted inside 
		vinPlan.pushValidators<CValidatorBase>((CInputIdentifierBase*)&normalInputIdentifier, &normalInputValidator);		// txfee - see AddNormalInput parameter
		vinPlan.pushValidators<CValidatorBase>((CInputIdentifierBase*)&ccInputIdentifier, &ownerCCaddrValidator);			// check cc owner addr

		// vout validation plan:
		voutPlan.pushValidators<CValidatorBase>(0, &cc1of2ValidatorThis);												// check 1of2 addr funding
		// do not check change at this time
		// no checking for opret yet
		break;

	case 'B': // add tokens
		// vin validation plan:
		//											we need cast here							this is casted inside 
		vinPlan.pushValidators<CValidatorBase>((CInputIdentifierBase*)&normalInputIdentifier, &normalInputValidatorLast);		// txfee - see AddNormalInput parameter
		vinPlan.pushValidators<CValidatorBase>((CInputIdentifierBase*)&ccInputIdentifier, &ownerCCaddrValidator);			// check cc owner addr

																															// vout validation plan:
		voutPlan.pushValidators<CValidatorBase>(0, &cc1of2ValidatorThis);												// check 1of2 addr funding
																															// do not check change at this time
		voutPlan.pushValidators<CValidatorBase>(numvouts - 1, &opRetValidator);												// opreturn check		
		break;

	case 'C':
	case 't':
		// vin validation plan:
		//											we need cast here							this is casted inside 
		vinPlan.pushValidators<CValidatorBase>((CInputIdentifierBase*)&normalInputIdentifier, &normalInputValidatorLast);		// txfee - see AddNormalInput parameter
		vinPlan.pushValidators<CValidatorBase>((CInputIdentifierBase*)&ccInputIdentifier, &cc1of2ValidatorThis, &cc1of2ValidatorLast);		// cc1of2 funding addr

		// vout validation plan:
		voutPlan.pushValidators<CValidatorBase>(0, &heirSpendValidator);													// check if heir is allowed to spend
		voutPlan.pushValidators<CValidatorBase>(numvouts - 1, &opRetValidator);												// opreturn check	
		break;
	}

	// call vin/vout validation
	if (!vinPlan.validate(tx, eval))
		return false;
	if (!voutPlan.validate(tx, eval))
		return false;

	return true;
}

/**
 * Tx validation entry function
 */
bool HeirValidate(struct CCcontract_info* cp, Eval* eval, const CTransaction& tx, uint32_t nIn)
{
    int32_t numvins = tx.vin.size();
    int32_t numvouts = tx.vout.size();
    //int32_t preventCCvins = -1;
    //int32_t preventCCvouts = -1;

    if (numvouts < 1)
        return eval->Invalid("no vouts");

    uint8_t funcId;
    uint256 txidInOpret = zeroid, latestTxid = zeroid, assetid = zeroid;
    
	CScript opRetScript = tx.vout[numvouts - 1].scriptPubKey;

	CScript latestTxOpRetScript;
	bool isHeirSpendingBegan = false;

	int32_t heirType = HEIR_NOTFOUND;
	funcId = DecodeHeirOpRet<CoinHelper>(opRetScript, assetid, txidInOpret, true);
	if(funcId != 0)
		heirType = HEIR_COINS;
	else  {
		funcId = DecodeHeirOpRet<TokenHelper>(opRetScript, assetid, txidInOpret);
		if (funcId != 0)
			heirType = HEIR_TOKENS;
	}
		
	if (heirType == HEIR_NOTFOUND)
        return eval->Invalid("invalid opreturn format");

    if (funcId != 'F' && funcId != 'G') {
        if (txidInOpret == zeroid) {
            return eval->Invalid("invalid tx opreturn format: no fundingtxid present");
        }
		if (heirType == HEIR_COINS)
			latestTxid = FindLatestFundingTx<CoinHelper>(txidInOpret, assetid, latestTxOpRetScript, isHeirSpendingBegan);
		else
			latestTxid = FindLatestFundingTx<TokenHelper>(txidInOpret, assetid, latestTxOpRetScript, isHeirSpendingBegan);

		if (latestTxid == zeroid) {
            return eval->Invalid("invalid heir transaction: no funding tx found");
        }
    } 

	// validate prev tx cc inputs = outputs:
	if (heirType == HEIR_TOKENS && funcId == 't' && !HeirExactTokenAmounts(true, cp, eval, zeroid, tx)) {
		std::cerr << "HeirValidate() this tx or some of its vin tx has invalid cc amounts" << std::endl;
		return eval->Invalid("this tx or some of its vin tx has invalid cc amounts");
	}

    switch (funcId) {
    case 'F': // fund coins:
              // vins.*: normal inputs
			  // -----------------------------
              // vout.0: funding CC 1of2 addr for the owner and heir
              // vout.1: txfee for CC addr used as a marker
              // vout.2: normal change
              // vout.n-1: opreturn 'F' ownerpk heirpk inactivitytime heirname

		return eval->Invalid("unexpected HeirValidate for heirfund");
		// break;

	case 'G': // fund tokens:
			  // vin.0: normal inputs txfee
			  // vins.1+: user's CC addr inputs 
			  // -----------------------
			  // vout.0: funding heir CC 1of2 addr for the owner and heir
			  // vout.1: txfee for CC addr used as a marker
			  // vout.2: normal change
			  // vout.n-1: opreturn 'G' ownerpk heirpk inactivitytime heirname assetid
		if (heirType == HEIR_TOKENS)
			return RunValidationPlans<TokenHelper>(funcId, cp, eval, tx, opRetScript, latestTxid, latestTxOpRetScript, isHeirSpendingBegan);
		else
			return eval->Invalid("unexpected funcid G");
		return true;  // TODO: impl validation
		// break;

    case 'A': // add funding coins:
              // vins.*: normal inputs
			  // ------------------------
              // vout.0: funding CC 1of2 addr for the owner and heir
              // vout.1: normal change
              // vout.n-1: opreturn 'A' ownerpk heirpk inactivitytime fundingtx 

		return eval->Invalid("unexpected HeirValidate for heiradd");
		// break;

	case 'B': // add funding tokens:
			  // vins.0: normal inputs txfee
			  // vins.1+: user's CC addr inputs 
			  // ------------------------
			  // vout.0: funding CC 1of2 addr for the owner and heir
			  // vout.1: normal change
			  // vout.n-1: opreturn 'B' ownerpk heirpk inactivitytime fundingtx 
		if (heirType == HEIR_TOKENS)
			return RunValidationPlans<TokenHelper>(funcId, cp, eval, tx, opRetScript, latestTxid, latestTxOpRetScript, isHeirSpendingBegan);
		else
			return eval->Invalid("unexpected funcid B");
		//break;

    case 'C': // claim coins:
			  // vin.0: normal input txfee
			  // vin.1+: input from CC 1of2 addr
			  // -------------------------------------
			  // vout.0: normal output to owner or heir address
			  // vout.1: change to CC 1of2 addr
			  // vout.2: change to user's addr from txfee input if any
			  // vout.n-1: opreturn 'C' ownerpk heirpk inactivitytime fundingtx
		if (heirType == HEIR_COINS) 
			return RunValidationPlans<CoinHelper>(funcId, cp, eval, tx, opRetScript,  latestTxid, latestTxOpRetScript, isHeirSpendingBegan);	
		else
			return eval->Invalid("unexpected funcid C");
		//  break;

	case 't': // claim tokens:
              // vin.0: normal input txfee
              // vin.1+: input from CC 1of2 addr
			  // --------------------------------------------
              // vout.0: output to user's EVAL_ASSETS address
			  // vout.1: change to CC 1of2 addr
			  // vout.2: change to normal from txfee input if any
              // vout.n-1: opreturn 't' assetid ownerpk heirpk inactivitytime fundingtx		
		if (heirType == HEIR_TOKENS)
			return RunValidationPlans<TokenHelper>(funcId, cp, eval, tx, opRetScript, latestTxid, latestTxOpRetScript, isHeirSpendingBegan);
		else
			return eval->Invalid("unexpected funcid t");
		// break;

    default:
		std::cerr << "HeirValidate() illegal heir funcid=" << (char)funcId << std::endl;
        return eval->Invalid("unexpected HeirValidate funcid");
        // break;
    }
    return eval->Invalid("unexpected");   //    (PreventCC(eval, tx, preventCCvins, numvins, preventCCvouts, numvouts));
}
// end of consensus code


// helper functions used in implementations of rpc calls (in rpcwallet.cpp) or validation code

/**
* Checks if vout is to cryptocondition address
* @return vout value in satoshis
*/
int64_t IsHeirFundingVout(struct CCcontract_info* cp, const CTransaction& tx, int32_t voutIndex, CPubKey ownerPubkey, CPubKey heirPubkey)
{
	char destaddr[65], heirContractAddr[65];

	GetCCaddress1of2(cp, heirContractAddr, ownerPubkey, heirPubkey);
	if (tx.vout[voutIndex].scriptPubKey.IsPayToCryptoCondition() != 0) {
		// NOTE: dimxy it was unsafe 'Getscriptaddress(destaddr,tx.vout[voutIndex].scriptPubKey) > 0' here:
		if (Getscriptaddress(destaddr, tx.vout[voutIndex].scriptPubKey) && strcmp(destaddr, heirContractAddr) == 0)
			return (tx.vout[voutIndex].nValue);
	}
	return (0);
}

// not used
bool HeirExactAmounts(struct CCcontract_info* cp, Eval* eval, const CTransaction& tx, int32_t minage, uint64_t txfee)
{
	static uint256 zerohash;
	CTransaction vinTx;
	uint256 hashBlock, activehash;
	int32_t i, numvins, numvouts;
	int64_t inputs = 0, outputs = 0, assetoshis;
	numvins = tx.vin.size();
	numvouts = tx.vout.size();
	for (i = 0; i < numvins; i++) {
		//fprintf(stderr,"HeirExactAmounts() vini.%d\n",i);
		if ((*cp->ismyvin)(tx.vin[i].scriptSig) != 0) {
			//fprintf(stderr,"HeirExactAmounts() vini.%d check mempool\n",i);
			if (eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0)
				return eval->Invalid("cant find vinTx");
			else {
				//fprintf(stderr,"HeirExactAmounts() vini.%d check hash and vout\n",i);
				if (hashBlock == zerohash)
					return eval->Invalid("cant Heir from mempool");
				////if ( (assetoshis= IsHeirCCvout(cp,vinTx,tx.vin[i].prevout.n)) != 0 )
				////    inputs += assetoshis;
			}
		}
	}
	for (i = 0; i < numvouts; i++) {
		//fprintf(stderr,"i.%d of numvouts.%d\n",i,numvouts);
		////if ( (assetoshis= IsHeirvout(cp,tx,i)) != 0 )
		////    outputs += assetoshis;
	}
	if (inputs != outputs + txfee) {
		fprintf(stderr, "HeirExactAmounts() inputs %llu vs outputs %llu\n", (long long)inputs, (long long)outputs);
		return eval->Invalid("mismatched inputs != outputs + txfee");
	}
	else
		return (true);
}

// makes coin initial tx opret
CScript EncodeHeirCreateOpRet(uint8_t eval, uint8_t funcid, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string heirName)
{
	return CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)eval << (uint8_t)funcid << ownerPubkey << heirPubkey << inactivityTimeSec << heirName);
}

// makes coin additional tx opret
CScript EncodeHeirOpRet(uint8_t eval, uint8_t funcid,  CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 fundingtxid)
{
	fundingtxid = revuint256(fundingtxid);
	return CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)eval << (uint8_t)funcid << ownerPubkey << heirPubkey << inactivityTimeSec << fundingtxid);
}
// makes opret for tokens while they are inside Heir contract address space - initial funding
CScript EncodeHeirAssetsCreateOpRet(uint8_t eval, uint8_t funcid, uint256 assetid, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string hearName)
{
	assetid = revuint256(assetid);
	return CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)eval << (uint8_t)funcid << assetid << ownerPubkey << heirPubkey << inactivityTimeSec << hearName);
}
// makes opret for tokens while they are inside Heir contract address space - additional funding
CScript EncodeHeirAssetsOpRet(uint8_t eval, uint8_t funcid, uint256 assetid, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 fundingtxid)
{
	assetid = revuint256(assetid);
	fundingtxid = revuint256(fundingtxid);
	return CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)eval << (uint8_t)funcid << assetid << ownerPubkey << heirPubkey << inactivityTimeSec << fundingtxid);
}
// makes opret for tokens being converted to Asset contract address space (change eval code = EVAL_ASSETS)
CScript EncodeHeirConvertedAssetsOpRet(uint8_t eval, uint8_t funcid, uint256 assetid, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 fundingtxid)
{
	assetid = revuint256(assetid);
	fundingtxid = revuint256(fundingtxid);
	return CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)eval << (uint8_t)funcid << assetid  << ownerPubkey << heirPubkey << inactivityTimeSec << fundingtxid);
}


/**
* decode opret vout for Heir contract
*/
template <class Helper> uint8_t DecodeHeirOpRet(CScript scriptPubKey, uint256 &assetid, CPubKey& ownerPubkey, CPubKey& heirPubkey, int64_t& inactivityTime, std::string& heirName, uint256& txidInOpret, bool noLogging)
{
	std::vector<uint8_t> vopret;
	uint8_t e = 0;
	uint8_t funcId = 0;

	txidInOpret = zeroid; //to init

	GetOpReturnData(scriptPubKey, vopret);

	if (vopret.size() > 1) {
		//E_UNMARSHAL(vopret, { ss >> e; ss >> funcId; ss >> ownerPubkey; ss >> heirPubkey; ss >> inactivityTime; if (IS_CHARINSTR(funcId, "F")) { ss >> heirName; } if (IS_CHARINSTR(funcId, "AC")) { ss >> txidInOpret; } });
		Helper::UnmarshalOpret(vopret, e, funcId, assetid, ownerPubkey, heirPubkey, inactivityTime, heirName, txidInOpret);
		
		/*
		std::cerr << "DecodeHeirOpRet() e=" << (int)e 
		<< " funcId=" << (char)funcId 
		<< " ownerPubkey=" << HexStr(ownerPubkey)
		<< " heirPubkey=" << HexStr(heirPubkey)
		<< " heirName=" << heirName << " inactivityTime=" << inactivityTime << '\n';
		*/

		//if (e == EVAL_HEIR && IS_CHARINSTR(funcId, "FAC"))
		if (e == Helper::getMyOpretEval() && Helper::isMyFuncId(funcId)) {
			assetid = revuint256(assetid);
			txidInOpret = revuint256(txidInOpret);
			return funcId;
		}
		else
		{
			if(!noLogging) std::cerr << "DecodeHeirOpRet() warning unexpected OP_RETURN eval=" << (int)e << " or field type=" << (char)funcId << '\n';
		}
	}
	else {
		std::cerr << "DecodeHeirOpRet() unmarshal error (vopret.size() == 0)" << '\n';
	}
	return (uint8_t)0;
}

/**
* overload for 'F' opret
*/
template <class Helper> uint8_t DecodeHeirOpRet(CScript scriptPubKey, uint256 &assetid, CPubKey& ownerPubkey, CPubKey& heirPubkey, int64_t& inactivityTime, std::string& heirName, bool noLogging)
{
	uint256 dummytxid;

	return DecodeHeirOpRet<Helper>(scriptPubKey, assetid, ownerPubkey, heirPubkey, inactivityTime, heirName, dummytxid, noLogging);
}

/**
* overload for using in AddHeirContractInputs
*/
template <class Helper> uint8_t DecodeHeirOpRet(CScript scriptPubKey, uint256 &assetid, uint256& txidInOpret, bool noLogging)
{
	CPubKey dummyOwnerPubkey, dummyHeirPubkey;
	int64_t dummyInactivityTime;
	std::string dummyHeirName;

	return DecodeHeirOpRet<Helper>(scriptPubKey, assetid, dummyOwnerPubkey, dummyHeirPubkey, dummyInactivityTime, dummyHeirName, txidInOpret, noLogging);
}



/**
 * find the latest funding tx: it may be the first F tx or one of A or C tx's 
 * Note: this function is also called from validation code (use non-locking calls) 
 */
template <class Helper> uint256 FindLatestFundingTx(uint256 fundingtxid, uint8_t& funcId, uint256 &assetid, CPubKey& ownerPubkey, CPubKey& heirPubkey, int64_t& inactivityTime, std::string& heirName, CScript& opRetScript, bool &isHeirSpendingBegan)
{
	CTransaction fundingtx;
	uint256 hashBlock;
    const bool allowSlow = false;

    //char markeraddr[64];
    //CCtxidaddr(markeraddr, fundingtxid);
    //SetCCunspents(unspentOutputs, markeraddr);

	isHeirSpendingBegan = false; //init the var
	funcId = 0; //init the var

    // get initial funding tx and set it as initial lasttx:
    if (myGetTransaction(fundingtxid, fundingtx, hashBlock) && fundingtx.vout.size()) {
        uint256 dummytxid;

        // set ownerPubkey and heirPubkey:
        if ((funcId = DecodeHeirOpRet<Helper>(fundingtx.vout[fundingtx.vout.size() - 1].scriptPubKey, assetid, ownerPubkey, heirPubkey, inactivityTime, heirName)) != 0) {
            // found at least funding tx!
            std::cerr << "FindLatestFundingTx() lasttx currently is fundingtx, txid=" << fundingtxid.GetHex() << " opreturn type=" << (char)funcId << '\n';
            opRetScript = fundingtx.vout[fundingtx.vout.size() - 1].scriptPubKey;
        } else {
            std::cerr << "FindLatestFundingTx() could not decode opreturn for fundingtxid=" << fundingtxid.GetHex() << '\n';
            return zeroid;
        }
    } else {
        std::cerr << "FindLatestFundingTx() could not find funding tx for fundingtxid=" << fundingtxid.GetHex() << '\n';
        return zeroid;
    }

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_HEIR);
    char coinaddr[64];
    GetCCaddress1of2(cp, coinaddr, ownerPubkey, heirPubkey); // get the address of cryptocondition '1 of 2 pubkeys'
                                                             
    SetCCunspents(unspentOutputs, coinaddr);				 // get vector with tx's with unspent vouts of 1of2pubkey address:
    //std::cerr << "FindLatestFundingTx() using 1of2address=" << coinaddr << " unspentOutputs.size()=" << unspentOutputs.size() << '\n';

    int32_t maxBlockHeight = 0; // max block height
    uint256 latesttxid = fundingtxid;

    // try to find the last funding or spending tx by checking fundingtxid in 'opreturn':
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        CTransaction regtx;
        uint256 hash;

        uint256 txid = it->first.txhash;
        //std::cerr << "FindLatestFundingTx() checking unspents for txid=" << txid.GetHex() << '\n';

        int32_t blockHeight = (int32_t)it->second.blockHeight;
        uint256 txidInOpret;

		//NOTE: maybe called from validation code:
        if (myGetTransaction(txid, regtx, hash)) {
            //std::cerr << "FindLatestFundingTx() found tx for txid=" << txid.GetHex() << " blockHeight=" << blockHeight << " dblockHeight=" << dblockHeight << '\n';

			uint256 dummyAssetid;  // not to contaminate the assetid from the params!
			uint8_t tmpFuncId;

            //std::cerr << "FindLatestFundingTx() regtx.vout.size()=" << regtx.vout.size() << " isDecoded=" << isDecoded << " funcId=" << funcId << " txidInOpret=" << txidInOpret.GetHex() << '\n';
            if (regtx.vout.size() > 0 && 
				(tmpFuncId = DecodeHeirOpRet<Helper>(regtx.vout[regtx.vout.size() - 1].scriptPubKey, dummyAssetid, txidInOpret, true)) != 0 &&
				fundingtxid == txidInOpret) {

				// check if heir has begun spending:
				if (Helper::isSpendingTx(tmpFuncId)) {  // if 'C' or 't' opret 
					const CScript heirScriptPubkey = CScript() << ParseHex(HexStr(heirPubkey)) << OP_CHECKSIG;

					for (int32_t v = 0; v < regtx.vout.size() - 1; v++) { // do not check opret vout
						if (heirScriptPubkey == regtx.vout[v].scriptPubKey)
							isHeirSpendingBegan = true;
					}
				}

                if (blockHeight > maxBlockHeight) {
                    maxBlockHeight = blockHeight;
                    latesttxid = txid;
                    opRetScript = regtx.vout[regtx.vout.size() - 1].scriptPubKey;
					funcId = tmpFuncId;
                    std::cerr << "FindLatestFundingTx() txid=" << latesttxid.GetHex() << " at blockHeight=" << maxBlockHeight << " opreturn type=" << (char)funcId << " set as current lasttxid" << '\n';
                }
            }
        }
    }

    return latesttxid;
}

// overload for validation code
template <class Helper> uint256 FindLatestFundingTx(uint256 fundingtxid, uint256 &assetid, CScript& opRetScript, bool &isHeirSpendingBegan)
{
    uint8_t funcId;
    CPubKey ownerPubkey;
    CPubKey heirPubkey;
    int64_t inactivityTime;
    std::string heirName;

    return FindLatestFundingTx<Helper>(fundingtxid, funcId, assetid, ownerPubkey, heirPubkey, inactivityTime, heirName, opRetScript, isHeirSpendingBegan);
}

// overload for transaction creation code
template <class Helper> uint256 FindLatestFundingTx(uint256 fundingtxid, uint8_t& funcId, uint256 &assetid, CPubKey& ownerPubkey, CPubKey& heirPubkey, int64_t& inactivityTime, std::string& heirName, bool &isHeirSpendingBegan)
{
    CScript opRetScript;

    return FindLatestFundingTx<Helper>(fundingtxid, funcId, assetid, ownerPubkey, heirPubkey, inactivityTime, heirName, opRetScript, isHeirSpendingBegan);
}

// add owner input in tokens
int64_t AddHeirTokenInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, CPubKey pk, uint256 refassetid, int64_t total, int32_t maxinputs)
{
	char coinaddr[64], destaddr[64]; int64_t threshold, nValue, price, totalinputs = 0; uint256 assetid, txid, hashBlock; std::vector<uint8_t> origpubkey; std::vector<uint8_t> vopret; CTransaction vintx; int32_t j, vout, n = 0; uint8_t evalcode, funcid;
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
	GetCCaddress(cp, coinaddr, pk);
	SetCCunspents(unspentOutputs, coinaddr);
	threshold = total / (maxinputs + 1);
	
	//fprintf(stderr,"AddHeirTokenInputs() check cc addr=%s for token inputs\n",coinaddr);
	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
	{
		txid = it->first.txhash;
		vout = (int32_t)it->first.index;

		if (it->second.satoshis < threshold)
			continue;
		for (j = 0; j<mtx.vin.size(); j++)
			if (txid == mtx.vin[j].prevout.hash && vout == mtx.vin[j].prevout.n)
				break;
		if (j != mtx.vin.size())
			continue;

		if (GetTransaction(txid, vintx, hashBlock, false) != 0)
		{
			Getscriptaddress(destaddr, vintx.vout[vout].scriptPubKey);
			
			//std::cerr << "AddHeirTokenInputs() check destaddrs=" << destaddr << " vout=" << vout << " vintx.vout[vout].nValue=" << vintx.vout[vout].nValue << " it->second=" << it->second.satoshis  << std::endl;

			if (strcmp(destaddr, coinaddr) != 0 && strcmp(destaddr, cp->unspendableCCaddr) != 0 && strcmp(destaddr, cp->unspendableaddr2) != 0)
				continue;
			GetOpReturnData(vintx.vout[vintx.vout.size() - 1].scriptPubKey, vopret);
			
			// NOTE: special opret decode:
			// we should not check for E_UNMARSHAL return code because it returns false if not EOF, 
			// but we have our remainder after assetid!
			bool isEof = false;
			bool unmarshalResult = E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> assetid; isEof=ss.eof());
			if (unmarshalResult || !isEof)
			{
				assetid = revuint256(assetid);  // usually DecodeOpretXXX() funcs do this

				//std::cerr << "AddHeirTokenInputs() vout=" << vout << " evalcode=" << (int)evalcode << " cp->evalcode=" << (int)cp->evalcode << " funcid=" << (char)funcid << " check for refassetid=" << refassetid.GetHex() << " vs assetid in opret=" << assetid.GetHex() << " coins=" << (double)vintx.vout[vout].nValue / COIN << std::endl;
				if (assetid == refassetid && 
					IS_CHARINSTR(funcid, "tGB") &&  
					(nValue = vintx.vout[vout].nValue) > 0 && myIsutxo_spentinmempool(txid, vout) == 0)
				{
					std::cerr << "AddHeirTokenInputs() total=" << total << " maxinputs=" << maxinputs << " uxto value=" << it->second.satoshis << std::endl;
					if (total != 0 && maxinputs != 0)
						mtx.vin.push_back(CTxIn(txid, vout, CScript()));
					//nValue = it->second.satoshis;
					totalinputs += nValue;
					n++;
					if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
						break;
				}
			}
		}
	}
	return(totalinputs);
}

// add inputs of 1 of 2 cc address
template <class Helper> int64_t Add1of2AddressInputs(struct CCcontract_info* cp, uint256 fundingtxid, CMutableTransaction& mtx, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t total, int32_t maxinputs)
{
    // TODO: add threshold check
    int64_t nValue, voutValue, totalinputs = 0;
    CTransaction vintx;
    int32_t n = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;

    char coinaddr[64];
    GetCCaddress1of2(cp, coinaddr, ownerPubkey, heirPubkey); // get address of cryptocondition '1 pubkey of 2 pubkeys'
    SetCCunspents(unspentOutputs, coinaddr);

    //   char markeraddr[64];
    //   CCtxidaddr(markeraddr, fundingtxid);
    //   SetCCunspents(unspentOutputs, markeraddr);
    //std::cerr << "Add1of2AddressInputs() using 1of2addr=" << coinaddr << " unspentOutputs.size()=" << unspentOutputs.size() << '\n';

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        uint256 txid = it->first.txhash;
        uint256 hashBlock;
        int32_t voutIndex = (int32_t)it->first.index;
        // no need to prevent dup
        // dimxy: maybe it is good to put tx's in cache?
        if (GetTransaction(txid, vintx, hashBlock, false) != 0) {
			uint256 assetid;
            uint256 txidInOpret;

			uint8_t funcId = DecodeHeirOpRet<Helper>(vintx.vout[vintx.vout.size() - 1].scriptPubKey, assetid, txidInOpret, true);
            // note: it returns in in satoshis too...
            if ((txid == fundingtxid || txidInOpret == fundingtxid) && // we're spending only tx's in this funding plan
				funcId != 0 &&         
				Helper::isMyFuncId(funcId) &&     
				// deep validation for tokens:
				(typeid(Helper) == typeid(TokenHelper) && IsHeirvout(true, cp, nullptr, assetid, vintx, voutIndex) > 0)  &&
                (voutValue = IsHeirFundingVout(cp, vintx, voutIndex, ownerPubkey, heirPubkey)) > 0 &&
                !myIsutxo_spentinmempool(txid, voutIndex)) 
			{
                std::cerr << "Add1of2AddressInputs() voutValue=" << voutValue << " satoshis=" << it->second.satoshis << '\n';
                if (total != 0 && maxinputs != 0)
                    mtx.vin.push_back(CTxIn(txid, voutIndex, CScript()));
                nValue = it->second.satoshis;
                totalinputs += nValue;
                n++;
                if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
                    break;
            }
        }
    }
    return totalinputs;
}

/**
 * enumerate all tx's sending to CCHeir 1of2address and calc total lifetime funds 
 */
template <class Helper> int64_t LifetimeHeirContractFunds(struct CCcontract_info* cp, uint256 fundingtxid, CPubKey ownerPubkey, CPubKey heirPubkey)
{
    char coinaddr[64];
    GetCCaddress1of2(cp, coinaddr, ownerPubkey, heirPubkey); // get the address of cryptocondition '1 of 2 pubkeys'

    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndexes;
    SetCCtxids(addressIndexes, coinaddr);

    //fprintf(stderr,"LifetimeHeirContractFunds() scan lifetime of %s\n",coinaddr);
    int64_t total = 0;
    for (std::vector<std::pair<CAddressIndexKey, CAmount>>::const_iterator it = addressIndexes.begin(); it != addressIndexes.end(); it++) {
        uint256 hashBlock;
        uint256 txid = it->first.txhash;
        CTransaction tx;

        if (GetTransaction(txid, tx, hashBlock, false) && tx.vout.size() > 0) {
            uint8_t funcId;
			uint256 assetid;
            uint256 txidInOpret;
            const int32_t ivout = 0;

			funcId = DecodeHeirOpRet<Helper>(tx.vout[tx.vout.size() - 1].scriptPubKey, assetid, txidInOpret, true);

            //std::cerr << "LifetimeHeirContractFunds() found tx=" << txid.GetHex() << " vout[0].nValue=" << subtx.vout[ccVoutIdx].nValue << " opreturn=" << (char)funcId << '\n';

            if (funcId != 0 && (txid == fundingtxid || txidInOpret == fundingtxid) && Helper::isMyFuncId(funcId) && !Helper::isSpendingTx(funcId)
                /* && !myIsutxo_spentinmempool(txid, ccVoutIdx) */) // include also tx in mempool
            {
				total += it->second; // dont do this: tx.vout[ivout].nValue; // in vin[0] always is the pay to 1of2 addr (funding or change)
                std::cerr << "LifetimeHeirContractFunds() added tx=" << txid.GetHex() << " it->second=" << it->second << " vout[0].nValue=" << tx.vout[ivout].nValue << " opreturn=" << (char)funcId << '\n';
            }
        }
    }
    return (total);
}

/* rpc functions' implementation: */

/**
 * heirfund rpc call implementation
 * creates tx for initial funds deposit on cryptocondition address which locks funds for spending by either of address. 
 * and also for setting spending plan for the funds' owner and heir
 * @return fundingtxid handle for subsequent references to this heir funding plan
 */
template <typename Helper> std::string HeirFund(uint64_t txfee, int64_t amount, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 assetid)
{
    CMutableTransaction mtx;
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_HEIR);
    if (txfee == 0)
        txfee = 10000;

    //std::cerr << "HeirFund() amount=" << amount << " txfee=" << txfee << " heirPubkey IsValid()=" << heirPubkey.IsValid() << " inactivityTime(sec)=" << inactivityTimeSec << " assetid=" << assetid.GetHex() << std::endl;

	if (!heirPubkey.IsValid()) {
		std::cerr << "HeirFund() heirPubkey is not valid!" << std::endl;
		return std::string("");
	}

    CPubKey myPubkey = pubkey2pk(Mypubkey());

	if (AddNormalinputs(mtx, myPubkey, txfee, 3) > 0) { // txfee for miners
		int64_t inputs, change;

		if ((inputs=Helper::addOwnerInputs(cp, assetid, mtx, myPubkey, amount, (int32_t)64)) > 0) { // 2 x txfee: 1st for marker vout, 2nd to miners
			//mtx.vout.push_back(MakeCC1vout(EVAL_HEIR,amount,HeirCCpk));
			mtx.vout.push_back(MakeCC1of2vout(EVAL_HEIR, amount, myPubkey, heirPubkey)); // add cryptocondition to spend amount for either pk

			// add a marker for finding all plans in HeirList()
			CPubKey HeirContractPubKey = GetUnspendable(cp, 0);
			mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(HeirContractPubKey)) << OP_CHECKSIG));   // TODO: do we need this marker?

		    // calc and add change vout:
			if (inputs > amount)
				change = (inputs - amount); //  -txfee <-- txfee pays user

			//std::cerr << "HeirFund() inputs=" << inputs << " amount=" << amount << " txfee=" << txfee << " change=" << change << '\n';

			if (change != 0) {	// vout[1]
				mtx.vout.push_back(Helper::makeUserVout(change, myPubkey));
			}

			// add change for txfee and opreturn vouts and sign tx:
			return (FinalizeCCTx(0, cp, mtx, myPubkey, txfee,
				// CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)EVAL_HEIR << (uint8_t)'F' << myPubkey << heirPubkey << inactivityTimeSec << heirName)));
				Helper::makeCreateOpRet(assetid, myPubkey, heirPubkey, inactivityTimeSec, heirName)));
		}
		else  // TODO: need result return unification with heiradd and claim
			std::cerr << "HeirFund() could not find owner inputs" << std::endl;

	}
	else
		std::cerr << "HeirFund() could not find normal inputs" << std::endl;
    return std::string("");
}

// if no these callers - it could not link 
std::string HeirFundCoinCaller(uint64_t txfee, int64_t funds, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 assetid){
	return HeirFund<CoinHelper>(txfee, funds, heirName, heirPubkey, inactivityTimeSec,  assetid);
}

std::string HeirFundTokenCaller(uint64_t txfee, int64_t funds, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 assetid) {
	return HeirFund<TokenHelper>(txfee, funds, heirName, heirPubkey, inactivityTimeSec, assetid);
}

/**
 * heiradd rpc call implementation
 * creates tx to add more funds to cryptocondition address for spending by either funds' owner or heir
 * @return result object with raw tx or error text
 */
template <class Helper> UniValue HeirAdd(uint256 fundingtxid, uint64_t txfee, int64_t amount)
{
    UniValue result(UniValue::VOBJ); //, a(UniValue::VARR);
    CMutableTransaction mtx;
    CPubKey ownerPubkey, heirPubkey;
    int64_t inputs, CCchange = 0;
    int64_t inactivityTimeSec;
    struct CCcontract_info *cp, C;
    std::string rawhex;
    uint256 lasttxid, assetid;
    std::string heirName;
    uint8_t funcId;
	bool isHeirSpendingBegan = false;

    cp = CCinit(&C, EVAL_HEIR);
    if (txfee == 0)
        txfee = 10000;

    if ((lasttxid = FindLatestFundingTx<Helper>(fundingtxid, funcId, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, isHeirSpendingBegan)) != zeroid) {
        int32_t numblocks;

        CPubKey myPubkey = pubkey2pk(Mypubkey());

        // check if it is the owner
        if (myPubkey != ownerPubkey) {
            result.push_back(Pair("result", "error"));
            result.push_back(Pair("error", "adding funds is only allowed for the owner of this contract"));
            return result;
        }

		if (AddNormalinputs(mtx, myPubkey, txfee, 3) > 0) { // txfee for miners

			int64_t inputs, change;

			//if (AddNormalinputs(mtx, myPubkey, amount + 1 * txfee, 64) > 0) { // TODO: why 64 max inputs?
			if ((inputs = Helper::addOwnerInputs(cp, assetid, mtx, myPubkey, amount, 64)) > 0) { // TODO: why 64 max inputs?

				// we do not use markers anymore - storing data in opreturn is better
					// add marker vout:
					/* char markeraddr[64];
					CPubKey markerpubkey = CCtxidaddr(markeraddr, fundingtxid);
					mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(markerpubkey)) << OP_CHECKSIG)); // txfee 1, txfee 2 - for miners
					std::cerr << "HeirAdd() adding markeraddr=" << markeraddr << '\n'; */

				// add cryptocondition to spend this funded amount for either pk
				mtx.vout.push_back(MakeCC1of2vout(EVAL_HEIR, amount, ownerPubkey, heirPubkey)); // using always pubkeys from OP_RETURN in order to not mixing them up!

																								// calc and add change vout:
				if (inputs > amount)
					change = (inputs - amount); //  -txfee <-- txfee pays user

				//std::cerr << "HeirAdd() inputs=" << inputs << " amount=" << amount << " txfee=" << txfee << " change=" << change << '\n';

				if (change != 0) {																		// vout[1]
					mtx.vout.push_back(Helper::makeUserVout(change, myPubkey));
				}

				// add opreturn 'F' or 'G' and sign tx:						// this txfee ignored
				std::string rawhextx = (FinalizeCCTx(0, cp, mtx, myPubkey, txfee,
					// CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)EVAL_HEIR << (uint8_t)'A' << ownerPubkey << heirPubkey << inactivityTimeSec << fundingtxid)));
					Helper::makeAddOpRet(assetid, ownerPubkey, heirPubkey, inactivityTimeSec, fundingtxid)));

				result.push_back(Pair("result", "success"));
				result.push_back(Pair("hextx", rawhextx));
			}
			else {
				std::cerr << "HeirAdd cannot find owner inputs" << std::endl;
				result.push_back(Pair("result", "error"));
				result.push_back(Pair("error", "can't find owner inputs"));
			}
		}
		else {
			std::cerr << "HeirAdd cannot find normal inputs for tx fee" << std::endl;
			result.push_back(Pair("result", "error"));
			result.push_back(Pair("error", "can't find normal inputs for tx fee"));
		}

    } else {
        fprintf(stderr, "HeirAdd() can't find any heir CC funding tx's\n");

        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "can't find any heir CC funding transactions"));
    }

    return result;
}


UniValue HeirAddCoinCaller(uint256 fundingtxid, uint64_t txfee, int64_t amount) {
	return HeirAdd<CoinHelper>(fundingtxid,  txfee,  amount);
}
UniValue HeirAddTokenCaller(uint256 fundingtxid, uint64_t txfee, int64_t amount) {
	return HeirAdd<TokenHelper>(fundingtxid, txfee, amount);
}


/**
 * heirclaim rpc call implementation
 * creates tx to spend funds from cryptocondition address by either funds' owner or heir
 * @return result object with raw tx or error text
 */
template <typename Helper>UniValue HeirClaim(uint256 fundingtxid, uint64_t txfee, int64_t amount)
{
    UniValue result(UniValue::VOBJ); //, a(UniValue::VARR);
    CMutableTransaction mtx;
    CPubKey myPubkey, ownerPubkey, heirPubkey;
    int64_t inputs, change = 0;
    int64_t inactivityTimeSec;
    struct CCcontract_info *cp, C;

    uint256 latesttxid, assetid;
    uint8_t funcId;
    std::string heirName;
	bool isHeirSpendingBegan = false;


    cp = CCinit(&C, EVAL_HEIR);
    if (txfee == 0)
        txfee = 10000;

    if ((latesttxid = FindLatestFundingTx<Helper>(fundingtxid, funcId, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, isHeirSpendingBegan)) != zeroid) {
        int32_t numblocks;
        uint64_t durationSec = 0;

		// we do not need to find duration if spending already has begun
		if (!isHeirSpendingBegan) {
            durationSec = CCduration(numblocks, latesttxid);
            std::cerr << "HeirClaim() duration=" << durationSec << " inactivityTime=" << inactivityTimeSec << " numblocks=" << numblocks << std::endl;
        }

        // spending is allowed if there is already spending tx or inactivity time
        //bool isAllowedToHeir = (funcId == 'C' || durationSec > inactivityTimeSec) ? true : false;
		bool isAllowedToHeir = (isHeirSpendingBegan || durationSec > inactivityTimeSec) ? true : false;
        myPubkey = pubkey2pk(Mypubkey());

        // if it is the heir, check if spending not allowed to heir yet
        if (myPubkey == heirPubkey && !isAllowedToHeir) {
            result.push_back(Pair("result", "error"));
            result.push_back(Pair("error", "spending is not allowed yet for the heir"));
            return result;
        }

		// we do not use markers any more:
			// we allow owner to spend funds at any time:
			// if it is the owner, check if spending already allowed to heir
			/* if (myPubkey == ownerPubkey && isAllowedToHeir) {
				result.push_back(Pair("result", "spending is not already allowed for the owner"));
				return result;
			}	*/

		// add spending txfee from the calling user
        if (AddNormalinputs(mtx, myPubkey, txfee, 3) > 0) {

			// add spending from cc 1of2 address
			if ((inputs = Add1of2AddressInputs<Helper>(cp, fundingtxid, mtx, ownerPubkey, heirPubkey, amount, 60)) >= amount) // TODO: why only 60 inputs?
            {
				/*if (inputs < amount) {
					std::cerr << "HeirClaim() cant find enough HeirCC 1of2 inputs, found=" << inputs << " required=" << amount << std::endl;
					result.push_back(Pair("result", "error"));
					result.push_back(Pair("error", "can't find heir CC funding"));

					return result;
				}*/

				// add vout with amount to claiming address
				mtx.vout.push_back(Helper::makeClaimerVout(amount, myPubkey));  // vout[0]

                // calc and add change vout:
                if (inputs > amount)
					change = (inputs - amount); //  -txfee <-- txfee pays user

                //std::cerr << "HeirClaim() inputs=" << inputs << " amount=" << amount << " txfee=" << txfee << " change=" << change << '\n';

				// change to 1of2 funding addr:
                if (change != 0) {											   // vout[1]
                    mtx.vout.push_back(MakeCC1of2vout(EVAL_HEIR, change, ownerPubkey, heirPubkey)); // using always pubkeys from OP_RETURN in order to not mixing them up!
                }

                // add marker vout:
                /*char markeraddr[64];
				CPubKey markerpubkey = CCtxidaddr(markeraddr, fundingtxid);
				// NOTE: amount = 0 is not working: causes error code: -26, error message : 64 : dust
				mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(markerpubkey)) << OP_CHECKSIG)); // txfee 1, txfee 2 - for miners
				std::cerr << "HeirClaim() adding markeraddr=" << markeraddr << '\n'; */

                uint8_t myprivkey[32];
                char coinaddr[64];
                // set priv key addresses in CC structure:
                GetCCaddress1of2(cp, coinaddr, ownerPubkey, heirPubkey);
                Myprivkey(myprivkey);

                //fprintf(stderr,"HeirClaim() before setting unspendable CC addr2= (%s) addr3= (%s)\n", cp->unspendableaddr2, cp->unspendableaddr3);
                CCaddr2set(cp, EVAL_HEIR, ownerPubkey, myprivkey, coinaddr);
                CCaddr3set(cp, EVAL_HEIR, heirPubkey, myprivkey, coinaddr);

                fprintf(stderr, "HeirClaim() after  setting unspendable CC addr2=(%s) addr3=(%s)\n", cp->unspendableaddr2, cp->unspendableaddr3);

                // add opreturn 'C' and sign tx:				  // this txfee will be ignored
				std::string rawhextx = FinalizeCCTx(0, cp, mtx, myPubkey, txfee,
					//CScript() << OP_RETURN << E_MARSHAL(ss << (uint8_t)EVAL_HEIR << (uint8_t)'C' << ownerPubkey << heirPubkey << inactivityTimeSec << fundingtxid));
					Helper::makeClaimOpRet(assetid, ownerPubkey, heirPubkey, inactivityTimeSec, fundingtxid));

                result.push_back(Pair("result", "success"));
                result.push_back(Pair("hextx", rawhextx));

            } else {
                fprintf(stderr, "HeirClaim() cant find HeirCC inputs\n");
                result.push_back(Pair("result", "error"));
                result.push_back(Pair("error", "can't find heir CC funding"));
            }
        } else {
            fprintf(stderr, "HeirClaim() cant find sufficient user inputs for tx fee\n");
            result.push_back(Pair("result", "error"));
            result.push_back(Pair("error", "can't find sufficient user inputs to pay transaction fee"));
        }
    } else {
        fprintf(stderr, "HeirClaim() can't find any heir CC funding tx's\n");
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "can't find any heir CC funding transactions"));
    }

    return result;
}

UniValue HeirClaimCoinCaller(uint256 fundingtxid, uint64_t txfee, int64_t amount) {
	return HeirClaim<CoinHelper>(fundingtxid, txfee, amount);
}
UniValue HeirClaimTokenCaller(uint256 fundingtxid, uint64_t txfee, int64_t amount) {
	return HeirClaim<TokenHelper>(fundingtxid, txfee, amount);
}



/**
 * heirinfo rpc call implementation
 * returns some information about heir CC contract plan by a handle of initial fundingtxid: 
 * plan name, owner and heir pubkeys, funds deposited and available, flag if spending is enabled for the heir
 * @return heir info data
 */
UniValue HeirInfo(uint256 fundingtxid)
{
    UniValue result(UniValue::VOBJ);

    CMutableTransaction mtx;
    CPubKey ownerPubkey, heirPubkey;
    uint256 latesttxid;
	uint256 assetid;

    std::string heirName;
    uint8_t funcId;
    int64_t inactivityTimeSec;


	CTransaction fundingtx;
	uint256 hashBlock;
	const bool allowSlow = false;

	//char markeraddr[64];
	//CCtxidaddr(markeraddr, fundingtxid);
	//SetCCunspents(unspentOutputs, markeraddr);

	// get initial funding tx and set it as initial lasttx:
	if (myGetTransaction(fundingtxid, fundingtx, hashBlock) && fundingtx.vout.size()) {

		int32_t heirType = HEIR_NOTFOUND;
		if (DecodeHeirOpRet<CoinHelper>(fundingtx.vout[fundingtx.vout.size() - 1].scriptPubKey, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, true) == 'F')
			heirType = HEIR_COINS;
		else if (DecodeHeirOpRet<TokenHelper>(fundingtx.vout[fundingtx.vout.size() - 1].scriptPubKey, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName) == 'G')
			heirType = HEIR_TOKENS;
		else
		{
			std::cerr << "HeirInfo() initial tx F or G not found for this txid" <<  std::endl;
			result.push_back(Pair("result", "error"));
			result.push_back(Pair("error", "initial tx F or G not found"));
			return result;
		}

		struct CCcontract_info *cp, C;
		cp = CCinit(&C, EVAL_HEIR);

		bool isHeirSpendingBegan = false;

		if (heirType == HEIR_COINS)
			latesttxid = FindLatestFundingTx<CoinHelper>(fundingtxid, funcId, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, isHeirSpendingBegan);
		else
			latesttxid = FindLatestFundingTx<TokenHelper>(fundingtxid, funcId, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, isHeirSpendingBegan);

		if (latesttxid != zeroid) {
			int32_t numblocks;
			uint64_t durationSec = 0;

			std::cerr << "HeirInfo() latesttxid=" << latesttxid.GetHex() << '\n';

			std::ostringstream stream;
			std::string msg;

			result.push_back(Pair("fundingtxid", fundingtxid.GetHex()));
			result.push_back(Pair("name", heirName.c_str()));

			if (heirType == HEIR_TOKENS) {
				stream << assetid.GetHex();
				msg = "assetid";
				result.push_back(Pair(msg, stream.str().c_str()));
				stream.str("");
				stream.clear();
			}

			char hexbuf[67];
			stream << pubkey33_str(hexbuf, (uint8_t*)ownerPubkey.begin());
			result.push_back(Pair("owner", stream.str().c_str()));
			stream.str("");
			stream.clear();

			stream << pubkey33_str(hexbuf, (uint8_t*)heirPubkey.begin());
			result.push_back(Pair("heir", stream.str().c_str()));
			stream.str("");
			stream.clear();

			int64_t total;
			if (heirType == HEIR_COINS)
				total = LifetimeHeirContractFunds<CoinHelper>(cp, fundingtxid, ownerPubkey, heirPubkey);
			else
				total = LifetimeHeirContractFunds<TokenHelper>(cp, fundingtxid, ownerPubkey, heirPubkey);

			if (heirType == HEIR_COINS) {
				msg = "funding total in coins";
				stream << (double)total / COIN;
			}
			else	{
				msg = "funding total in tokens";
				stream << total;
			}
			result.push_back(Pair(msg, stream.str().c_str()));
			stream.str("");
			stream.clear();

			int64_t inputs;
			if (heirType == HEIR_COINS)
				inputs = Add1of2AddressInputs<CoinHelper>(cp, fundingtxid, mtx, ownerPubkey, heirPubkey, 0, 60); //NOTE: amount = 0 means all unspent inputs
			else
				inputs = Add1of2AddressInputs<TokenHelper>(cp, fundingtxid, mtx, ownerPubkey, heirPubkey, 0, 60);

			if (heirType == HEIR_COINS) {
				msg = "funding available in coins";
				stream << (double)inputs / COIN;
			}
			else	{
				msg = "funding available in tokens";
				stream << inputs;
			}
			result.push_back(Pair(msg, stream.str().c_str()));
			stream.str("");
			stream.clear();

			if (heirType == HEIR_TOKENS) {
				int64_t ownerInputs = TokenHelper::addOwnerInputs(cp, assetid, mtx, ownerPubkey, 0, (int32_t)64);
				stream << ownerInputs;
				msg = "owner funding available in tokens";
				result.push_back(Pair(msg, stream.str().c_str()));
				stream.str("");
				stream.clear();
			}

			stream << inactivityTimeSec;
			result.push_back(Pair("inactivity time setting", stream.str().c_str()));
			stream.str("");
			stream.clear();

			if (!isHeirSpendingBegan) { // we do not need find duration if the spending already has begun
				durationSec = CCduration(numblocks, latesttxid);
				std::cerr << "HeirInfo() duration=" << durationSec << " inactivityTime=" << inactivityTimeSec << " numblocks=" << numblocks << '\n';
			}

			stream << std::boolalpha << (isHeirSpendingBegan || durationSec > inactivityTimeSec);
			result.push_back(Pair("spending allowed for the heir", stream.str().c_str()));
			stream.str("");
			stream.clear();

			result.push_back(Pair("result", "success"));
		}
		else {
			result.push_back(Pair("result", "error"));
			result.push_back(Pair("error", "could not find heir cc plan for this txid"));
		}
	}
	else {
		result.push_back(Pair("result", "error"));
		result.push_back(Pair("error", "could not find heir cc plan for this txid (no initial tx)"));
	}
	return (result);
}

/**
 * heirlist rpc call implementation
 * @return list of heir plan handles (fundingtxid)
 */
UniValue HeirList()
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_HEIR);

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    char coinaddr[64];
    CPubKey ccPubKeyEmpty;
    GetCCaddress(cp, coinaddr, ccPubKeyEmpty);
    SetCCunspents(unspentOutputs, cp->normaladdr);

    std::cerr << "HeirList() finding heir marker from Heir contract addr=" << cp->normaladdr << " unspentOutputs.size()=" << unspentOutputs.size() << '\n';

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        uint256 hashBlock;
        uint256 txid = it->first.txhash;
		uint256 assetid;
        int32_t vout = (int32_t)it->first.index;

        std::cerr << "HeirList() checking txid=" << txid.GetHex() << " vout=" << vout << '\n';

        CTransaction vintx;
        if (GetTransaction(txid, vintx, hashBlock, false) != 0 && (vintx.vout.size() - 1) > 0) {
            CPubKey ownerPubkey, heirPubkey;
            std::string heirName;
            int64_t inactivityTimeSec;
            uint256 txidInOpret;

			uint8_t funcId = DecodeHeirOpRet<CoinHelper>(vintx.vout[vintx.vout.size() - 1].scriptPubKey, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, txidInOpret);
			if (funcId == 0)
				funcId = DecodeHeirOpRet<TokenHelper>(vintx.vout[vintx.vout.size() - 1].scriptPubKey, assetid, ownerPubkey, heirPubkey, inactivityTimeSec, heirName, txidInOpret);

            if (funcId == 'F' || funcId == 'G') {
                result.push_back(Pair("fundingtxid heirName", txid.GetHex() + std::string(" ") + heirName));
            }
			else {
				fprintf(stderr, "HeirList() couldnt find initial transaction\n");
			}
        } else {
            fprintf(stderr, "HeirList() couldnt load transaction\n");
        }
    }
    return result;
}




#define NO_NORMAL_CHANGE 0x1
#define NO_CC_CHANGE 0x2
std::string MakeBadTokenTxToSelf(uint256 assetid, int64_t amount);
/**
 * makes bad tx for tests on validation code
 */
std::string Heir_MakeBadTx(uint256 fundingtxid, uint8_t funcId,  int64_t amount, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTime, uint32_t errMask)
{
	//UniValue result(UniValue::VOBJ);

	CMutableTransaction mtx;
	uint256 lasttxid;
	std::string heirName = "test";
	int64_t txfee = 10000;

	// route to make bad token tx:
	if (errMask & 0x10)
		return MakeBadTokenTxToSelf(fundingtxid, amount);

	struct CCcontract_info *cp, C;
	cp = CCinit(&C, EVAL_HEIR);

	CPubKey myPubkey = pubkey2pk(Mypubkey());

	/*for (int i = 0; i < normalInputTxids.size(); i++) {
		mtx.vin.push_back(CTxIn(normalInputTxids[i], normalInputVouts[i], CScript()));
		std::cerr << "Heir_MakeBadTx() added normal vin txid=" << normalInputTxids[i].GetHex() << " vout_i=" << normalInputVouts[i] << std::endl;
	}

	for (int i = 0; i < ccTxids.size(); i++) {
		mtx.vin.push_back(CTxIn(ccTxids[i], ccVouts[i], CScript()));
		std::cerr << "Heir_MakeBadTx() added cc vin txid=" << ccTxids[i].GetHex() << " vout_i=" << ccVouts[i] << std::endl;
	}*/

	std::cerr << "Heir_MakeBadTx() funcid=" << (char)funcId << " owner=" << HexStr(ownerPubkey) << " heir=" << HexStr(heirPubkey) << " inactivityTime=" << inactivityTime << std::endl;

	int64_t normalInputs, normalChange;
	int64_t ccInputs, CCchange;

	normalInputs = AddNormalinputs(mtx, myPubkey, (funcId == 'C') ? txfee : (amount + (funcId == 'F' ? 2 : 1)*txfee), 60);
	std::cerr << "Heir_MakeBadTx() added normalInputs=" << normalInputs << std::endl;

	if (funcId == 'F' || funcId == 'A') {

		mtx.vout.push_back(MakeCC1of2vout(EVAL_HEIR, (errMask & NO_NORMAL_CHANGE) ? (normalInputs - txfee) : amount, myPubkey, heirPubkey)); // add cryptocondition to spend amount for either pk
		if((errMask & NO_NORMAL_CHANGE) )
			std::cerr << "Heir_MakeBadTx() mask = no normal change" << std::endl;

		if (funcId == 'F') {
			CPubKey HeirCCPubKey = GetUnspendable(cp, 0);
			mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(HeirCCPubKey)) << OP_CHECKSIG));   // add a marker for finding all plans in HeirList()
		}
	}


	if (funcId == 'C') {
		ccInputs = Add1of2AddressInputs<CoinHelper>(cp, fundingtxid, mtx, ownerPubkey, heirPubkey, amount, 60);
		std::cerr << "Heir_MakeBadTx() added ccInputs=" << ccInputs << std::endl;
	
		// add standard scriptPubKey to self
		mtx.vout.push_back(CTxOut((errMask & NO_CC_CHANGE) ? ccInputs : amount, CScript() << ParseHex(HexStr(myPubkey)) << OP_CHECKSIG));

		if (!(errMask & NO_CC_CHANGE)) {	// no cc change
			if (ccInputs > amount)
				CCchange = (ccInputs - amount);
			std::cerr << "HeirClaim() CCchange=" << CCchange << std::endl;
			if (CCchange != 0) { // vout[0]
				mtx.vout.push_back(MakeCC1of2vout(EVAL_HEIR, CCchange, ownerPubkey, heirPubkey)); // using always pubkeys from OP_RETURN in order to not mixing them up!
			}
		}
		else {
			std::cerr << "Heir_MakeBadTx() mask = no CC change" << std::endl;
		}

	}

	CScript opret;
	if (funcId == 'F') {
		opret << OP_RETURN << E_MARSHAL(ss << (uint8_t)EVAL_HEIR << (uint8_t)'F' << ownerPubkey << heirPubkey << inactivityTime << heirName);
		std::cerr << "Heir_MakeBadTx() for 'F' using heir name=" << heirName << std::endl;
	}
	else  if (funcId == 'A'|| funcId == 'C')
		opret << OP_RETURN << E_MARSHAL(ss << (uint8_t)EVAL_HEIR << (uint8_t)funcId << ownerPubkey << heirPubkey << inactivityTime << fundingtxid);

	// spending from cc to self
	if (funcId == 'C') {
		uint8_t myprivkey[32];
		char coinaddr[64];
		// set priv key addresses in CC structure:
		GetCCaddress1of2(cp, coinaddr, ownerPubkey, heirPubkey);
		Myprivkey(myprivkey);

		//fprintf(stderr,"before setting unspendable CC addr2= (%s) addr3= (%s)\n", cp->unspendableaddr2, cp->unspendableaddr3);
		CCaddr2set(cp, EVAL_HEIR, ownerPubkey, myprivkey, coinaddr);
		CCaddr3set(cp, EVAL_HEIR, heirPubkey, myprivkey, coinaddr);

		fprintf(stderr, "Heir_MakeBadTx() after setting unspendable CC addr2= (%s) addr3= (%s)\n", cp->unspendableaddr2, cp->unspendableaddr3);
	}



	return (FinalizeCCTx(0, cp, mtx, myPubkey, txfee, opret ));
}

// make bad token tx
// returns a tx creating fake tokens
std::string MakeBadTokenTxToSelf(uint256 assetid, int64_t amount) {

	int64_t txfee = 10000;

	struct CCcontract_info *cp, C;
	cp = CCinit(&C, EVAL_ASSETS);

	CMutableTransaction mtx;
	CPubKey myPubkey = pubkey2pk(Mypubkey());
	int64_t normalInputs = AddNormalinputs(mtx, myPubkey, txfee + amount, 60);
	std::cerr << "MakeBadTokenTxToSelf() added normalInputs=" << normalInputs << std::endl;


	//int64_t CCchange = (normalInputs - amount - txfee);
	mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, amount, myPubkey));

	return(FinalizeCCTx(0, cp, mtx, myPubkey, txfee, EncodeAssetOpRet('t', assetid, zeroid, 0, Mypubkey())));
}