// vim:ts=4:sw=4:noet
#include "ADCClient.h"
#include "Hub.h"
#include "TigerHash.h"
#include "Encoder.h"
#include "Plugin.h"
#include "UserInfo.h"
#include "UserData.h"
#include "qhub.h"
#include "ADC.h"

using namespace std;
using namespace qhub;

#define CMD(a, b, c) (((u_int32_t)a<<8) | (((u_int32_t)b)<<16) | (((u_int32_t)c)<<24))
u_int32_t ADCClient::Commands[] = {
		CMD('C','T','M'),
		CMD('D','S','C'),
		CMD('G','E','T'),
		CMD('G','F','I'),
		CMD('G','P','A'),
		CMD('I','N','F'),
		CMD('M','S','G'),
		CMD('N','T','D'),
		CMD('P','A','S'),
		CMD('Q','U','I'),
		CMD('R','C','M'),
		CMD('R','E','S'),
		CMD('S','C','H'),
		CMD('S','N','D'),
		CMD('S','T','A'),
		CMD('S','U','P')
};
#undef CMD

ADCClient::ADCClient(int fd, Domain domain, Hub* parent) throw()
: ADCSocket(fd, domain, parent), added(false), userData(NULL), userInfo(NULL), state(PROTOCOL), udpActive(false)
{
	onConnected();
}

ADCClient::~ADCClient() throw()
{
	if(userInfo)
		delete userInfo;
	if(userData)
		delete userData;
}

UserData* ADCClient::getUserData() throw()
{
	if(!userData)
		userData = new UserData;
	return userData;
}

void ADCClient::onAlarm() throw()
{
	if(!disconnected) {
		// Do a silent disconnect. We don't want to show our protocol to an unknown peer.
		disconnect();
	} else {
		realDisconnect();
	}
}

void ADCClient::login() throw()
{
	alarm(0); // Stop alarm. Else we'd get booted.
	added = true;
	state = NORMAL;
	Plugin::UserConnected action;
	Plugin::fire(action, this);
	if(action.isSet(Plugin::DISCONNECTED))
		return;
	// Send INFs
	getHub()->getUserList(this);
	// Add user
	udpActive = userInfo->isUdpActive();
	if(udpActive)
		getHub()->addActiveClient(getCID32(), this);
	else
		getHub()->addPassiveClient(getCID32(), this);
	// Notify him that userlist is over and notify others of his presence
	getHub()->broadcast(getAdcInf());
	getHub()->motd(this);
}

void ADCClient::logout() throw()
{
	getHub()->removeClient(getCID32());
	added = false;
	Plugin::UserDisconnected action;
	Plugin::fire(action, this);
}

string const& ADCClient::assemble(StringList const& sl) throw()
{
	return ADC::toString(sl, temp);
}

string const& ADCClient::getAdcInf() throw()
{
	return userInfo->toADC(getCID32());
}


/*******************************************/
/* Calls from ADCSocket (and other places) */
/*******************************************/

void ADCClient::doAskPassword(string const& pwd) throw()
{
	assert(state == IDENTIFY && !added);
	password = pwd;
	salt = Util::genRand192();
	send("IGPA " + getHub()->getCID32() + ' ' + Encoder::toBase32(salt.data(), salt.length()) + '\n');
	state = VERIFY;
}

void ADCClient::doWarning(string const& msg) throw()
{
	send("ISTA " + getCID32() + " 100 " + ADC::ESC(msg) + '\n');
}

void ADCClient::doError(string const& msg) throw()
{
	send("ISTA " + getCID32() + " 200 " + ADC::ESC(msg) + '\n');
}

void ADCClient::doDisconnect(string const& msg) throw()
{
	if(added) {
		logout();
		if(msg.empty())
			getHub()->broadcast("IQUI " + getCID32() + " ND\n");
		else
			getHub()->broadcast("IQUI " + getCID32() + " DI " + getHub()->getCID32() + ' ' + ADC::ESC(msg) + '\n');
	}
	disconnect();
}

void ADCClient::doHubMessage(string const& msg) throw()
{
	send("BMSG " + getHub()->getCID32() + ' ' + ADC::ESC(msg) + '\n');
}

void ADCClient::doPrivateMessage(string const& msg) throw()
{
	send("DMSG " + getCID32() + ' ' + getHub()->getCID32() + ' ' + ADC::ESC(msg) + " PM\n");
}

void ADCClient::doDisconnectBy(string const& kicker, bool silent, string const& msg) throw()
{
	string full = "IQUI " + getCID32() + " DI " + kicker + ' ' + msg + '\n';
	send(full);
	logout();
	if(silent) {
		getHub()->broadcast("IQUI " + getCID32() + " ND\n");
	} else {
		getHub()->broadcast(full);
	}
	disconnect();
}

void ADCClient::doKickBy(string const& kicker, bool silent, string const& msg) throw()
{
	string full = "IQUI " + getCID32() + " KK " + kicker + ' ' + ADC::ESC(msg) + '\n';
	send(full);
	logout();
	if(silent) {
		getHub()->broadcast("IQUI " + getCID32() + " ND\n");
	} else {
		getHub()->broadcast(full);
	}
	disconnect();
}
	
void ADCClient::doBanBy(string const& kicker, bool silent, u_int32_t seconds, string const& msg) throw()
{
	// TODO add bantime to somewhere
	string full = "IQUI " + getCID32() + " BN " + kicker + ' ' + Util::toString(seconds) + ' ' + ADC::ESC(msg) + '\n';
	send(full);
	logout();
	if(silent) {
		getHub()->broadcast("IQUI " + getCID32() + " ND\n");
	} else {
		getHub()->broadcast(full);
	}
	disconnect();
}

void ADCClient::doRedirectBy(string const& kicker, bool silent, string const& address, string const& msg) throw()
{
	string full = "IQUI " + getCID32() + " RD " + kicker + ' ' + ADC::ESC(address) + ' ' + ADC::ESC(msg) + '\n';
	send(full);
	logout();
	if(silent) {
		getHub()->broadcast("IQUI " + getCID32() + " ND\n");
	} else {
		getHub()->broadcast(full);
	}
	disconnect();
}
	

#define PROTOCOL_ERROR(errmsg) \
	do { \
		doError(errmsg); \
		doDisconnect(errmsg); \
	} while(0)

void ADCClient::onLine(StringList& sl, string const& full) throw()
{		
	assert(!sl.empty());
	fprintf(stderr, "<< %s", full.c_str());

	// Totally crappy data is rewarded with a silent disconnect
	if(sl[0].size() != 4) {
		doDisconnect();
		return;
	}

	// Make Command integer
	u_int32_t command = stringToFourCC(sl[0]);
	string const* fullmsg = &full;

	// Plugin fire ClientLine
	{
		Plugin::ClientLine action;
		Plugin::fire(action, this, command, sl);
		if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED)) {
			return;
		} else if(action.isSet(Plugin::MODIFIED)) {
			command = stringToFourCC(sl[0]);
			fullmsg = &assemble(sl);
		}
	}

	// Do specialized input checking
	switch(command & 0x000000FF) {
	case 'H':
		// ? do we wish supports to happen just whenever or at PROTOCOL only ?
		if(state == PROTOCOL && (command & 0xFFFFFF00) == Commands[SUP]) {
			handleSupports(sl);
			return;
		// ? same with HPAS ?
		} else if(state == VERIFY && (command & 0xFFFFFF00) == Commands[PAS] && sl.size() == 3 && sl[1] == getCID32()) {
			handlePassword(sl);
			return;
		}
		break;
	case 'B':
		if(state == IDENTIFY && (command & 0xFFFFFF00) == Commands[INF] && sl.size() >= 2) {
			handleLogin(sl);
			return;
		}
		break;
	default:
		break;
	}

	// All non-NORMAL states have been handled
	if(state == PROTOCOL) {
		// Do a silent disconnect, we don't want scanners to find us that easily.
		assert(!added);
		disconnect();
		return;
	} else if(state != NORMAL) {
		PROTOCOL_ERROR("State mismatch: NORMAL expected");
		return;
	}

	// CID must come second (or third)
	if(
			!((command & 0x000000FF) != 'D' && sl.size() >= 2 && sl[1] == getCID32()) &&
			!((command & 0x000000FF) == 'D' && sl.size() >= 3 && sl[2] == getCID32())
	) {
		PROTOCOL_ERROR("CID mismatch: " + getCID32() + " expected");
		return;
	}

	// Check message type
	switch(command & 0x000000FF) {
	case 'A':
	case 'B':
	case 'D':
	case 'H':
	case 'P':
		break;
	case 'C':
	case 'I':
	case 'U':
	default:
		PROTOCOL_ERROR(string("Message type unsupported: ") + (char)(command & 0x000000FF) + " recieved");
		return;
	}
	
	// Woohoo! A normal message to process
	handle(sl, command, fullmsg);
}

void ADCClient::onConnected() throw()
{
	alarm(15);
	Plugin::ClientConnected action;
	Plugin::fire(action, this);
}

void ADCClient::onDisconnected(string const& clue) throw()
{
	alarm(15);
	if(added) {
		// this is here so ADCSocket can safely destroy us.
		// if we don't want a second message and our victim to get the message as well
		// remove us when doing e.g. the Kick, so that added is false here.
		fprintf(stderr, "onDisconnected %d %p GUID: %s\n", fd, this, getCID32().c_str());
		logout();
		if(clue.empty())
			getHub()->broadcast(string("IQUI " + getCID32() + " ND\n"));
		else
			getHub()->broadcast(string("IQUI " + getCID32() + " DI " + getCID32() + ' ' + ADC::ESC(clue) + '\n'));
	}
	Plugin::ClientDisconnected action;
	Plugin::fire(action, this);
}



/*****************/
/* Data handlers */
/*****************/

void ADCClient::handle(StringList& sl, u_int32_t const cmd, string const* full) throw() {
	u_int32_t threeCC = cmd & 0xFFFFFF00;
	char oneCC = (char)cmd & 0x000000FF;
	
	// Check if we need to handle anything, if not, do default action.

	// * HDSC *
	if(oneCC == 'H') {
		if(threeCC == Commands[DSC]) {
			handleDisconnect(sl);
			return;
		} else {
			doWarning("Unknown hub-directed message ignored");
			return;
		}
	// * BINF *
	} else if(threeCC == Commands[INF]) {
		if(oneCC == 'B') {
			handleInfo(sl, cmd, full);
			return;
		} else {
			doWarning("INF message type invalid");
			return;
		}
	// * ?MSG *
	} else if(threeCC == Commands[MSG]) {
		handleMessage(sl, cmd, full);
		return;
	// * Everything else *
	} else {
		switch(oneCC) {
		case 'A':
			getHub()->broadcastActive(*full);
			break;
		case 'B':
			getHub()->broadcast(*full); // do we ever want to stop anything to self?
			break;
		case 'D':
			getHub()->direct(sl[1], *full, this);
			break;
		case 'P':
			getHub()->broadcastPassive(*full);
			break;
		default:
			assert(0);
			break;
		}
	}
}

void ADCClient::handleLogin(StringList& sl) throw()
{
	assert(state == IDENTIFY);
	guid = sl[1];
	
	if(getHub()->hasClient(guid)) {
		PROTOCOL_ERROR("CID busy, change CID or wait");
		// Ping other user, perhaps it's a ghost
		getHub()->direct(guid, "INTD " + getCID32() + '\n');
		// Note: Don't forget to check again at HPAS.. perhaps someone beat us to it.
		return;
	}

	// Load info
	userInfo = new UserInfo(this);
	userInfo->fromADC(sl);

	// Guarantee NI and (I4 or I6)
	if(!userInfo->has(UIID('N','I'))) {
		PROTOCOL_ERROR("Missing parameters in INF");
		return;
	}

	// Broadcast	
	Plugin::ClientLogin action;
	Plugin::fire(action, this);
	if(action.isSet(Plugin::DISCONNECTED))
		return;
	
	if(password.empty())
		login();
}

void ADCClient::handleInfo(StringList& sl, u_int32_t const cmd, string const* full) throw()
{
	assert(state == NORMAL);

	UserInfo newUserInfo(this);
	newUserInfo.fromADC(sl);

	Plugin::ClientInfo action;
	Plugin::fire(action, this, newUserInfo);
	if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED))
		return;

	// Do redundancy check
	for(UserInfo::const_iterator i = newUserInfo.begin(); i != newUserInfo.end(); ++i) {
		if(userInfo->get(i->first) == i->second) {
			PROTOCOL_ERROR("Redundant INF parameter recieved");
			return;
		}
	}

	// Broadcast
	if(action.isSet(Plugin::MODIFIED)) {
		getHub()->broadcast(newUserInfo.toADC(getCID32()));
	} else if(!full) {
		getHub()->broadcast(assemble(sl));
	} else {
		getHub()->broadcast(*full);
	}

	// Merge new data
	userInfo->update(newUserInfo);	

	// Did we become active/passive just now?
	if(udpActive != userInfo->isUdpActive()) {
		udpActive = !udpActive;
		getHub()->switchClientMode(udpActive, getCID32(), this);
	}	
}

void ADCClient::handleMessage(StringList& sl, u_int32_t const cmd, string const* full) throw()
{
	char oneCC = cmd & 0x000000FF;
	if(oneCC == 'D' && sl.size() >= 4) {
		if(sl[1] == getHub()->getCID32()) {
			Plugin::UserCommand action;
			Plugin::fire(action, this, sl[3]);
			if(action.isSet(Plugin::DISCONNECTED))
				return;
			if(!action.isSet(Plugin::STOPPED)) {
				if(full && !action.isSet(Plugin::MODIFIED))
					send(*full);
				else
					send(assemble(sl));
			}
		} else {
			if(sl.size() == 4) {
				Plugin::UserMessage action;
				Plugin::fire(action, this, cmd, sl[3]);
				if(action.isSet(Plugin::DISCONNECTED))
					return;
				if(!action.isSet(Plugin::STOPPED)) {
					if(action.isSet(Plugin::MODIFIED))
						full = &assemble(sl);
					getHub()->direct(sl[1], *full, this);
				}
			} else {
				Plugin::UserPrivateMessage action;
				Plugin::fire(action, this, cmd, sl[3], sl[4]);
				if(action.isSet(Plugin::DISCONNECTED))
					return;
				if(!action.isSet(Plugin::STOPPED)) {
					if(action.isSet(Plugin::MODIFIED))
						full = &assemble(sl);
					getHub()->direct(sl[1], *full, this);
				}
			}
		}
	} else if(sl.size() >= 3) {
		if(sl.size() == 3) {
			Plugin::UserMessage action;
			Plugin::fire(action, this, cmd, sl[2]);
			if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED))
				return;
			if(action.isSet(Plugin::MODIFIED))
				full = &assemble(sl);
		} else {
			Plugin::UserPrivateMessage action;
			Plugin::fire(action, this, cmd, sl[2], sl[3]);
			if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED))
				return;
			if(action.isSet(Plugin::MODIFIED))
				full = &assemble(sl);
		}

		switch(oneCC) {
		case 'A':
			getHub()->broadcastActive(*full);
			break;
		case 'B':
			getHub()->broadcast(*full);
			break;
		case 'P':
			getHub()->broadcast(*full);
			break;
		default:
			assert(0);
		}
	} else {
		doWarning("Message parameters corrupt");
	}
}

void ADCClient::handleDisconnect(StringList& sl) throw()
{
	// HDSC myCID hisCID LL LL params
	if(!userInfo->getOp()) {
		doWarning("Access denied");
		return;
	}
	if(sl.size() < 6) {
		doWarning("Disconnect command corrupt");
		return;
	}
	bool silent = sl[4] == "ND";
	if(!(silent || sl[3] == sl[4])) {
		doWarning("Disconnect command corrupt");
		return;
	}
	// TODO add plugin stuff
	if(sl[3] == "DI" && sl.size() == 6) {
		getHub()->userDisconnect(sl[1], sl[2], silent, sl[5]);
	} else if(sl[3] == "KK" && sl.size() == 6) {
		getHub()->userKick(sl[1], sl[2], silent, sl[5]);
	} else if(sl[3] == "BN" && sl.size() == 7) {
		getHub()->userBan(sl[1], sl[2], silent, Util::toInt(sl[5]), sl[6]);
	} else if(sl[3] == "RD" && sl.size() == 7) {
		 getHub()->userRedirect(sl[1], sl[2], silent, sl[5], sl[6]);
	} else {
		doWarning("Disconnect command corrupt");
	}
}
	
void ADCClient::handlePassword(StringList& sl) throw()
{
	// Make CID from base32
	u_int64_t cid;
	Encoder::fromBase32(getCID32().c_str(), (u_int8_t*)&cid, sizeof(cid));
	// Make hash
	TigerHash h;
	h.update((u_int8_t*)&cid, sizeof(cid));
	h.update(password.data(), password.length());
	h.update(salt.data(), salt.length());
	h.finalize();
	string8 hashed_pwd(h.getResult(), TigerHash::HASH_SIZE);
	string hashed_pwd_b32 = Encoder::toBase32(hashed_pwd.data(), hashed_pwd.length());
	if(hashed_pwd_b32 != sl[2]) {
		send("ISTA " + sl[1] + " 223 Bad\\ username\\ or\\ password\n");
		assert(!added);
		disconnect();
		return;
	}
	salt.clear();
	// Add user
	if(getHub()->hasClient(getCID32())) {
		PROTOCOL_ERROR("CID busy, change CID or wait");
		return;
	}
	// Oki, do login
	password.clear();
	login();
}

void ADCClient::handleSupports(StringList& sl) throw()
{
	send("ISUP " + getHub()->getCID32() + " +BASE\n" // <-- do we need CID?
			"IINF " + getHub()->getCID32() + " NI" + ADC::ESC(getHub()->getHubName()) +
			" HU1 DEmajs VEqhub0.6 OP1\n");
	state = IDENTIFY;
}
