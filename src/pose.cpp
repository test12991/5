#include "pose.h"
#include "clientversion.h"
#include "chainparams.h"
#include "init.h"
#include "net.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include <boost/thread.hpp>

static int64_t nPovsProcessTime = 0;
static int64_t nSleepTime = 0;

void GetSanctuaryOrdinal(std::string sSancIP, int& out_ipos)
{
	out_ipos = 0;
	int iPos = 0;
	auto mnList = deterministicMNManager->GetListAtChainTip();
	mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) 
	{
		std::string sIP1 = dmn->pdmnState->addr.ToString();
		std::string sIP = GetElement(sIP1, ":", 0);
		if (sIP == sSancIP)
		{
			out_ipos = iPos;
			return;
		}
		iPos++;
	});
}

void ThreadPOVS(CConnman& connman)
{
	int nIterations = 0;
	while (1 == 1)
	{
	    if (ShutdownRequested())
			return;
		try
		{
			double nBanning = 1;
			bool fConnectivity = POVSTest("Status", "209.145.56.214:40000", 15, 2);
			LogPrintf("\r\nPOVS::Sanctuary Connectivity Test::%f", fConnectivity);
			bool fPOVSEnabled = nBanning == 1 && fConnectivity;
			int64_t nElapsed = GetAdjustedTime() - nPovsProcessTime;
			if (nElapsed > (60 * 60 * 24))
			{
				// Once every 24 hours we clear the POVS statuses and start over (in case sanctuaries dropped out or added, or if the entire POVS system was disabled etc).
				mapPOVSStatus.clear();
				nPovsProcessTime = GetAdjustedTime();
			}
			if (fPOVSEnabled)
			{
				auto mnList = deterministicMNManager->GetListAtChainTip();
				std::vector<uint256> toBan;

				int iPos = 0;
				mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) 
				{
					if (!ShutdownRequested())
					{
						std::string sPubKey = dmn->pdmnState->pubKeyOperator.Get().ToString();
						std::string sIP1 = dmn->pdmnState->addr.ToString();
						boost::this_thread::interruption_point();
						bool fOK = POVSTest(sPubKey, sIP1, 30, 0);
						int iSancOrdinal = 0;
						int nStatus = fOK ? 1 : 255;
						mapPOVSStatus[sPubKey] = nStatus;
						if (!fOK)
						{
					        toBan.emplace_back(dmn->proTxHash);
						}
						MilliSleep(1000);
						iPos++;
					}
				});
				// Ban 
				for (const auto& proTxHash : toBan) 
				{
					mnList.PoSePunish(proTxHash, mnList.CalcPenalty(100), false);
				}
			}
			nIterations++;
			int64_t nTipAge = GetAdjustedTime() - chainActive.Tip()->GetBlockTime();
		}
		catch(...)
		{
			LogPrintf("Error encountered in POVS main loop. %f \n", 0);
		}
		int nSleepLength = nIterations < 6 ? 60 * (nIterations + 1) : 60 * 20;
		
		for (int i = 0; i < nSleepLength; i++)
		{
			if (ShutdownRequested())
				break;
			MilliSleep(1000);
			nSleepTime++;
			if (nSleepTime > (60 * 15))
			{
				nSleepTime = 0;
			}
		}
	}
}

