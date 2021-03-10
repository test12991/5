#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "masternode-sync.h"

static int64_t nPoosProcessTime = 0;
static int64_t nSleepTime = 0;

//Request missing e-mails (Those less than 30 days old, paid for, in transactions, not on our hard drive)
void RequestMissingEmails()
{
	int iReq = 0;
	LogPrintf("\nRequestMissingEmails::Start %f ", GetAdjustedTime());

	for (auto ii : mvApplicationCache) 
	{
		if (Contains(ii.first, "EMAIL"))
		{
			std::pair<std::string, int64_t> v = mvApplicationCache[ii.first];
			int64_t nTimestamp = v.second;
			std::string sTXID = GetElement(ii.first, "[-]", 1);
			uint256 hashInput = uint256S(sTXID);
			std::string sFileName = "email_" + hashInput.GetHex() + ".eml";
			std::string sTarget = GetSANDirectory4() + sFileName;
			int64_t nSz = GETFILESIZE(sTarget);
			if (nSz <= 0)
			{
				//LogPrintf("\nSMTP::Requesting Missing Email %s ", sFileName);
				CEmailRequest erequest;
				erequest.RequestID = hashInput;
				bool nSent = false;
				g_connman->ForEachNode([&erequest, &nSent](CNode* pnode) 
				{
					erequest.RelayTo(pnode, *g_connman);
			    });
				//	LogPrintf("\nSMTP::RequestMissingEmails - Requesting %s %f ", sFileName, nSz);
			}
			else
			{
				//LogPrintf("\nSMTP::RequestMissingEmails - Found %s %f ", sFileName, nSz);
				std::map<uint256, CEmail>::iterator mi = mapEmails.find(hashInput);
				if (mi == mapEmails.end())
				{
					CEmail e;
					e.EDeserialize(hashInput);
					uint256 MyHash = e.GetHash();
					e.Body = std::string();
					mapEmails.insert(std::make_pair(MyHash, e));
				}
			}
		}
	}
	LogPrintf("\nRequestMissingEmails::End %f ", GetAdjustedTime());
}

void SanctuaryOracleProcess()
{
	// Sanctuary side UTXO Oracle Process
	std::vector<UTXOStake> uStakes = GetUTXOStakes(false);
	for (int i = 0; i < uStakes.size(); i++)
	{
		UTXOStake d = uStakes[i];
		if (d.found)
		{
			int nStatus = GetUTXOStatus(d.TXID);
			if (nStatus == 0)
			{
				AssimilateUTXO(d);
			}
		}
	}
	fUTXOSTested = true;
	// End of Sanctuary side UTXO Oracle Process
}

void ThreadPOOS(CConnman& connman)
{
	if (false)
		SyncSideChain(0);
	int nIterations = 0;

	while (1 == 1)
	{
	    if (ShutdownRequested())
			return;

		try
		{

			SanctuaryOracleProcess();

			double nOrphanBanning = GetSporkDouble("EnableOrphanSanctuaryBanning", 0);
			bool fConnectivity = POOSOrphanTest("status", 60 * 60);
			bool fPOOSEnabled = nOrphanBanning == 1 && fConnectivity;
			int64_t nElapsed = GetAdjustedTime() - nPoosProcessTime;
			if (nElapsed > (60 * 60 * 24))
			{
				// Once every 24 hours we clear the POOS statuses and start over (in case sanctuaries dropped out or added, or if the entire POOS system was disabled etc).
				mapPOOSStatus.clear();
				nPoosProcessTime = GetAdjustedTime();
				mapUTXOStatus.clear();
				fUTXOSTested = false;
				SanctuaryOracleProcess();
			}
			if (nOrphanBanning != 1)
			{
				mapPOOSStatus.clear();
			}

			if (fPOOSEnabled)
			{
				auto mnList = deterministicMNManager->GetListAtChainTip();
				std::vector<uint256> toBan;

				mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) 
				{
					if (!ShutdownRequested())
					{
						std::string sPubKey = dmn->pdmnState->pubKeyOperator.Get().ToString();
						bool fOK = POOSOrphanTest(sPubKey, 60 * 60);
						int nStatus = fOK ? 1 : 255;
						mapPOOSStatus[sPubKey] = nStatus;
						if (!fOK)
						{
					        toBan.emplace_back(dmn->proTxHash);
						}
						MilliSleep(1000);
					}
				});
				// Ban 
				for (const auto& proTxHash : toBan) {
					mnList.PoSePunish(proTxHash, mnList.CalcPenalty(100), false);
				}
			}
			nIterations++;
			int64_t nTipAge = GetAdjustedTime() - chainActive.Tip()->GetBlockTime();
			if (nTipAge < (60 * 60 * 4) && chainActive.Tip()->nHeight % 10 == 0)
			{
				if (false)
					SyncSideChain(chainActive.Tip()->nHeight);
			}
		}
		catch(...)
		{
			LogPrintf("Error encountered in POOS main loop. %f \n", 0);
		}
		int nSleepLength = nIterations < 6 ? 60 * (nIterations + 1) : 60 * 30;
		
		for (int i = 0; i < nSleepLength; i++)
		{
			if (ShutdownRequested())
				break;
			MilliSleep(1000);
			nSleepTime++;
			if (nSleepTime > (60 * 15))
			{
				nSleepTime = 0;
				RequestMissingEmails();
			}
		}
	}
}

