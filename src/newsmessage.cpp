//
// News system
//

#include <boost/foreach.hpp>
#include <map>

#include "newsmessage.h"
#include "key.h"
#include "net.h"
#include "sync.h"
#include "ui_interface.h"

using namespace std;

map<uint256, CNewsMessage> mapNewsMessages;
CCriticalSection cs_mapNewsMessages;

static const char* pszMainKey = "0411B6861C8E2B3499F5C0B0F9B2725A17306C549252DC42D58DF7EA73678E6BBAB9C85D02972B4FB072AA1FCF4881F5DB5D1EBC861770B3FDD82C4082CDFD3A03";
static const char* pszMainKey2 = "041CDA80B2F948CA57184E447979259523C389CF493A6A07F79C9A7E2635A2657966976A454940B0C4E9ACC209C83A205817E9DF26740B649949B605D0171B718A";

void CUnsignedNewsMessage::SetNull()
{
    nVersion = 1;
    nRelayUntil = 0;
    nExpiration = 0;
    nTime = 0;
    nID = 0;    
    nLanguage = 0; //English
    nPriority = 0;

    strHeader.clear();
    strMessage.clear();
    strTrayNotify.clear();
}

std::string CUnsignedNewsMessage::ToString() const
{    
    return strprintf(
        "CNewsMessage(\n"
        "    nVersion     = %d\n"
        "    nRelayUntil  = %"PRI64d"\n"
        "    nExpiration  = %"PRI64d"\n"
        "    nID          = %d\n"
        "    nTime          = %d\n"
        "    nLanguage          = %d\n"
        "    nPriority    = %d\n"
        "    strHeader   = \"%s\"\n"
        "    strTrayNotify = \"%s\"\n"
        ")\n",
        nVersion,
        nRelayUntil,
        nExpiration,
        nID,
        nTime,
        nLanguage,
        nPriority,
        strHeader.c_str(),
        strTrayNotify.c_str());
}

void CUnsignedNewsMessage::print() const
{
    printf("%s", ToString().c_str());
}

void CNewsMessage::SetNull()
{
    CUnsignedNewsMessage::SetNull();
    vchMsg.clear();
    vchSig.clear();
}

bool CNewsMessage::IsNull() const
{
    return (nExpiration == 0);
}

uint256 CNewsMessage::GetHash() const
{
    return Hash(this->vchMsg.begin(), this->vchMsg.end());
}

bool CNewsMessage::IsInEffect() const
{
    return (GetAdjustedTime() < nExpiration);
}

bool CNewsMessage::RelayTo(CNode* pnode) const
{
    if (!IsInEffect())
        return false;

    if (pnode->setKnown.insert(GetHash()).second)
    {
        if (GetAdjustedTime() < nRelayUntil)
        {
            pnode->PushMessage("news", *this);
            return true;
        }
    }
    return false;
}

bool CNewsMessage::CheckSignature() const
{
    CKey key, key2;
    bool ok = false;
    if (key.SetPubKey(ParseHex(pszMainKey)))
        ok = (key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig));
    if (!ok)
        if (key2.SetPubKey(ParseHex(pszMainKey2)))
            ok = (key2.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig));
    if (!ok)
        return error("CNewsMessage::CheckSignature() : verify signature failed");

    // Now unserialize the data
    CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
    sMsg >> *(CUnsignedNewsMessage*)this;
    return true;
}

CNewsMessage CNewsMessage::getMessageByHash(const uint256 &hash)
{
    CNewsMessage retval;
    {
        LOCK(cs_mapNewsMessages);
        map<uint256, CNewsMessage>::iterator mi = mapNewsMessages.find(hash);
        if(mi != mapNewsMessages.end())
            retval = mi->second;
    }
    return retval;
}

bool CNewsMessage::ProcessMessage()
{
    if (!CheckSignature())
        return false;
    if (!IsInEffect())
        return false;

    int maxInt = std::numeric_limits<int>::max();
    if (nID == maxInt)
    {
        if (!(nExpiration == maxInt && nPriority == maxInt))
            return false;
    }

    {
        LOCK(cs_mapNewsMessages);
        // Cancel previous messages
        for (map<uint256, CNewsMessage>::iterator mi = mapNewsMessages.begin(); mi != mapNewsMessages.end();)
        {
            const CNewsMessage& message = (*mi).second;
            if (!message.IsInEffect())
            {
                printf("expiring message %d\n", message.nID);
                uiInterface.NotifyNewsMessageChanged((*mi).first, CT_DELETED);
                mapNewsMessages.erase(mi++);
            }
            else
                mi++;
        }        

        // Add to mapNewsMessages
        mapNewsMessages.insert(make_pair(GetHash(), *this));
        //Notify UI if it applies to me
        uiInterface.NotifyNewsMessageChanged(GetHash(), CT_NEW);
    }    
    return true;
}
