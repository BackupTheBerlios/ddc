// vim:ts=4:sw=4:noet
#include "ADCClient.h"
#include "Hub.h"
#include "TigerHash.h"
#include "Encoder.h"
#include "Plugin.h"
#include "ADCInf.h"
#include "UserData.h"
#include "qhub.h"

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
: ADCSocket(fd, domain, parent), added(false), attributes(new ADCInf(this)), userData(new UserData), state(PROTOCOL)
{
	onConnected();
}

ADCClient::~ADCClient() throw()
{
	delete attributes;
	delete userData;
}

void ADCClient::onAlarm() throw()
{
	if(!disconnected) {
		// do a silent disconnect. we don't want to show our protocol
		// to an unknown peer.
		disconnect();
	} else {
		realDisconnect();
	}
}

void ADCClient::login() throw()
{
	alarm(0);
	added = true;
	state = NORMAL;
	Plugin::UserConnected action;
	Plugin::fire(action, this);
	if(action.isSet(Plugin::DISCONNECTED))
		return;
	// Send INFs
	getHub()->getUsersList(this);
	string tmp = attributes->getChangedInf();
	// Add user
	if(isUdpActive())
		getHub()->addActiveClient(getCID32(), this);
	else
		getHub()->addPassiveClient(getCID32(), this);
	// Notify him that userlist is over and notify others of his presence
	getHub()->broadcast(tmp);
	getHub()->motd(this);
}

void ADCClient::logout() throw()
{
	getHub()->removeClient(getCID32());
	added = false;
	Plugin::UserDisconnected action;
	Plugin::fire(action, this);
}

string const& ADCClient::getInf() const throw()
{
	return attributes->getFullInf();
}

bool ADCClient::isUdpActive() const throw()
{
	return !(attributes->getSetInf("U4").empty() && attributes->getSetInf("U6").empty());
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
	send("ISTA " + getCID32() + " 100 " + esc(msg) + '\n');
}

void ADCClient::doError(string const& msg) throw()
{
	send("ISTA " + getCID32() + " 200 " + esc(msg) + '\n');
}

void ADCClient::doDisconnect(string const& msg) throw()
{
	if(added) {
		logout();
		if(msg.empty())
			getHub()->broadcast("IQUI " + getCID32() + " ND\n");
		else
			getHub()->broadcast("IQUI " + getCID32() + " DI " + getHub()->getCID32() + ' ' + esc(msg) + '\n');
	}
	disconnect();
}

void ADCClient::doHubMessage(string const& msg) throw()
{
	send("BMSG " + getHub()->getCID32() + ' ' + esc(msg) + '\n');
}

void ADCClient::doPrivateMessage(string const& msg) throw()
{
	send("DMSG " + getCID32() + ' ' + getHub()->getCID32() + ' ' + esc(msg) + " PM\n");
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
	if(state != NORMAL) {
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
			getHub()->broadcast(string("IQUI " + getCID32() + " DI " + getCID32() + ' ' + esc(clue) + '\n'));
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
			handleInfo(sl);
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
		// FIXME: Send a ping see if we have a dangling connection (ghost)?
		// NOTE: Don't forget to check again at HPAS.. perhaps someone beat us to it.
		return;
	}

	attributes->setInf(sl);
	
	Plugin::ClientLogin action;
	Plugin::fire(action, this);
	if(action.isSet(Plugin::DISCONNECTED))
		return;
	
	if(password.empty())
		login();
}

void ADCClient::handleInfo(StringList& sl) throw()
{
	assert(state == NORMAL);
	attributes->setInf(sl);

	Plugin::ClientInfo action;
	Plugin::fire(action, this);
	if(action.isSet(Plugin::DISCONNECTED))
		return;
	
	bool isActive = isUdpActive();
	getHub()->broadcast(attributes->getChangedInf());
	if(isUdpActive() != isActive)
		getHub()->switchClientMode(isUdpActive(), getCID32(), this);
}
	
void ADCClient::handleMessage(StringList& sl, u_int32_t const cmd, string const* full) throw()
{
	char oneCC = cmd & 0x000000FF;
	if(oneCC == 'D' && sl.size() >= 4) {
		if(sl[1] == getHub()->getCID32()) {
			Plugin::UserCommand action;
			Plugin::fire(action, this, sl[3]);
			// FIXME, add 'reply'
			return;
		} else {
			if(sl.size() == 4) {
				Plugin::UserMessage action;
				Plugin::fire(action, this, cmd, sl[3]);
				if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED)) {
					return;
				} else if(action.isSet(Plugin::MODIFIED)) {
					full = &assemble(sl);
				}
			} else {
				Plugin::UserPrivateMessage action;
				Plugin::fire(action, this, cmd, sl[3], sl[4]);
				if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED)) {
					return;
				} else if(action.isSet(Plugin::MODIFIED)) {
					full = &assemble(sl);
				}
			}
			getHub()->direct(sl[1], *full, this);
			// FIXME, add 'reply'
		}
	} else if(sl.size() >= 3) {
		if(sl.size() == 3) {
			Plugin::UserMessage action;
			Plugin::fire(action, this, cmd, sl[2]);
			if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED)) {
				return;
			} else if(action.isSet(Plugin::MODIFIED)) {
				full = &assemble(sl);
			}
		} else {
			Plugin::UserPrivateMessage action;
			Plugin::fire(action, this, cmd, sl[2], sl[3]);
			if(action.isSet(Plugin::DISCONNECTED) || action.isSet(Plugin::STOPPED)) {
				return;
			} else if(action.isSet(Plugin::MODIFIED)) {
				full = &assemble(sl);
			}
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
		// FIXME, add 'reply'
	} else {
		doWarning("Message parameters corrupt");
	}
}

void ADCClient::handleDisconnect(StringList& sl) throw()
{
	if(attributes->getSetInf("OP").empty()) {
		doWarning("Access denied");
		return;
	}
	if(sl.size() >= 5) {
		bool hide = sl[4] == "ND";
		string const& victim_guid = sl[2];
		if(!hide && sl[3] != sl[4]) {
			PROTOCOL_ERROR("HDSC corrupt"); // "ND" or sl[3] should be at sl[4]
			return;
		}
		ADCClient* victim = getHub()->getClient(victim_guid);
		if(!victim)
			return;
		bool success = false;
		string msg = string("IQUI ") + victim_guid;
		if(sl.size() == 6) {
			if(sl[3] == "DI") {
				msg += " DI " + sl[1] + ' ' + esc(sl[5]) + '\n';
				victim->send(msg);
				success = true;
			} else if(sl[3] == "KK") {
				msg += " KK " + sl[1] + ' ' + esc(sl[5]) + '\n';
				victim->send(msg);
				// todo: set bantime
				success = true;
			}
		} else if(sl.size() == 7) {
			if(sl[3] == "BN") {
				msg += " BN " + sl[1] + ' ' + esc(sl[5]) + ' ' + esc(sl[6]) + '\n';
				victim->send(msg);
				// todo: set bantime
				success = true;
			} else if(sl[3] == "RD") {
				msg += " RD " + sl[1] + ' ' + esc(sl[5]) + ' ' + esc(sl[6]) + '\n';
				victim->send(msg);
				success = true;
			}
		}
		if(success) {
			// remove victim
			victim->logout();
			victim->disconnect();
			// notify everyone else
			if(!hide) {
				getHub()->broadcast(msg);
			} else {
				if(this != victim)
					send(msg); // notify self
				getHub()->broadcast("IQUI " + victim_guid + " ND\n", this);
			}
		} else {
			PROTOCOL_ERROR("HDSC corrupt");
		}
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
		send("ISTA " + sl[1] + " 23 Bad\\ username\\ or\\ password\n");
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
			"IINF " + getHub()->getCID32() + " NI" + esc(getHub()->getHubName()) +
			" HU1 DEmajs VEqhub0.5 OP1\n");
	state = IDENTIFY;
}
