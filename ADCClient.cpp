// vim:ts=4:sw=4:noet
#include "ADCClient.h"
#include "Hub.h"
#include "TigerHash.h"
#include "Encoder.h"
#include "Plugin.h"
#include "ADCInf.h"
#include "UserData.h"

using namespace std;
using namespace qhub;

ADCClient::ADCClient(int fd, Domain domain, Hub* parent) throw()
: ADCSocket(fd, domain), added(false), attributes(new ADCInf(this)), hub(parent), userData(new UserData), state(START),
		action(PROCEED), lastLine(NULL), lastParsed(NULL)
{
	onConnected();
}

ADCClient::~ADCClient() throw()
{
	delete attributes;
	delete userData;
}

void ADCClient::login() throw()
{
	//send infs
	hub->getUsersList(this);
	hub->addClient(guid, this);
	added = true;
	//notify him that userlist is over and notify others of his presence
	hub->broadcast(attributes->getChangedInf());
	hub->motd(this);
}

void ADCClient::logout() throw()
{
	hub->removeClient(guid);
	added = false;
}

string const& ADCClient::getInf() const throw()
{
	return attributes->getFullInf();
}

bool ADCClient::isActive() const throw()
{
	return !(attributes->getSetInf("U4").empty() && attributes->getSetInf("U6").empty());
}


#define PROTOCOL_ERROR(errmsg) \
	do { \
		doError(errmsg); \
		doDisconnect(errmsg); \
	} while(0)

/*******************************************/
/* Calls from ADCSocket (and other places) */
/*******************************************/

void ADCClient::doAskPassword(string const& pwd) throw()
{
	assert(state == IDENTIFY); // OR added == false
	password = pwd;
	salt = Util::genRand192();
	send("IGPA " + hub->getCID32() + ' ' + Encoder::toBase32(salt.data(), salt.length()) + '\n');
	state = VERIFY;
}

void ADCClient::doWarning(string const& msg) throw()
{
	send("ISTA " + guid + " 10 " + esc(msg) + '\n');
}

void ADCClient::doError(string const& msg) throw()
{
	send("ISTA " + guid + " 20 " + esc(msg) + '\n');
}

void ADCClient::doDisconnect(string const& msg) throw()
{
	if(added) {
		logout();
		if(msg.empty())
			hub->broadcast("IQUI " + guid + " ND\n");
		else
			hub->broadcast("IQUI " + guid + " DI " + hub->getCID32() + ' ' + esc(msg) + '\n');
	}
	disconnect();
}

void ADCClient::doHubMessage(string const& msg) throw()
{
	send("BMSG " + hub->getCID32() + ' ' + esc(msg) + '\n');
}

void ADCClient::doPrivateMessage(string const& msg) throw()
{
	send("DMSG " + guid + ' ' + hub->getCID32() + ' ' + esc(msg) + " PM\n");
}

void ADCClient::onLine(StringList& sl, string const& full) throw()
{		
	assert(!sl.empty());
	lastLine = &full;
	lastParsed = &sl;
	fprintf(stderr, "<< %s", full.c_str());
	// fire the all seeing plugin message
	initAction();
	Plugin::fire(Plugin::ClientLine(), this);
	if(getAction(STOP))
		return;
	assert(!getAction(MODIFIED));
	// Do basic input checking, state and guid
	if(sl[0].length() == 4) {
		switch(sl[0][0]) {
		case 'A':
			if(state == NORMAL && sl.size() >= 2 && sl[1] == guid) {
				handleA(sl, full);
			} else {
				PROTOCOL_ERROR("State, CID or parameter mismatch");
			}
			break;
		case 'B':
			if(sl.size() >= 2 && ((sl[1] == guid && state == NORMAL) || state == IDENTIFY)) {
				handleB(sl, full);
			} else {
				PROTOCOL_ERROR("State, CID or parameter mismatch");
			}
			break;
		case 'C': 
			PROTOCOL_ERROR("C type messages not supported");
			return;
		case 'D':
			if(state == NORMAL && sl.size() >= 3 && sl[2] == guid) {
				handleD(sl, full);
			} else {
				PROTOCOL_ERROR("State, CID or parameter mismatch");
			}
			break;
		case 'I':
			PROTOCOL_ERROR("I type messages not supported");
			return;
		case 'H':
			if(sl.size() >= 2 && (state == START || sl[1] == guid)) {
				handleH(sl, full);
			} else {
				PROTOCOL_ERROR("State, CID or parameter mismatch");
			}
			break;
		case 'P':
			if(state == NORMAL && sl.size() >= 2 && sl[1] == guid) {
				handleD(sl, full);
			} else {
				PROTOCOL_ERROR("State, CID or parameter mismatch");
			}
			break;
		case 'U':
			PROTOCOL_ERROR("U type messages not supported");
			break;
		default:
			PROTOCOL_ERROR("Message type not supported");
		}
	} else {
		PROTOCOL_ERROR("Illegal message");
	}
}

void ADCClient::onConnected() throw()
{
	initAction(NONE);
	Plugin::fire(Plugin::ClientConnected(), this);
}

void ADCClient::onDisconnected(string const& clue) throw()
{
	if(added) {
		// this is here so ADCSocket can safely destroy us.
		// if we don't want a second message and our victim to get the message as well
		// remove us when doing e.g. the Kick, so that added is false here.
		fprintf(stderr, "onDisconnected %d %p GUID: %s\n", fd, this, guid.c_str());
		logout();
		if(clue.empty())
			hub->broadcast(string("IQUI " + guid + " ND\n"), this);
		else
			hub->broadcast(string("IQUI " + guid + " DI " + guid + ' ' + esc(clue) + '\n'), this);
	}
	initAction(NONE);
	Plugin::fire(Plugin::ClientDisconnected(), this);
}



/*****************/
/* Data handlers */
/*****************/

void ADCClient::handleA(StringList& sl, string const& full) throw()
{
	// todo: check parameters, fix sending to active only
	if(sl[0] == "AMSG") {
		hub->broadcastActive(full);
	} else {
		PROTOCOL_ERROR(sl[0] + " message type or parameter count unsupported");
	}
}
	
void ADCClient::handleB(StringList& sl, string const& full) throw()
{
	if(state == IDENTIFY) {
		if(sl[0] == "BINF") {
			handleBINF(sl, full);
		} else {
			PROTOCOL_ERROR("State mismatch");
		}
	} else if(state == NORMAL) {
		if(sl[0] == "BINF") {
			handleBINF(sl, full);
		} else if(sl[0] == "BMSG" && (sl.size() == 3 || sl.size() == 4 /* flags */)) {
			handleBMSG(sl, full);
		} else if(sl[0] == "BSCH") {
			hub->broadcast(full, this);
		} else {
			PROTOCOL_ERROR(sl[0] + " message type or parameter count unsupported");
		}
	} else {
		PROTOCOL_ERROR("State mismatch");
	}
}

void ADCClient::handleBINF(StringList& sl, string const& full) throw()
{
	attributes->setInf(sl);

	if(state == IDENTIFY) {
		guid = sl[1];
		if(hub->hasClient(guid)) {
			//send a ping? to see if we have a dangling connection (ghost)?
			PROTOCOL_ERROR("CID busy, change CID or wait");
			//don't forget to check again at HPAS.. perhaps someone beat us to it
			//(don't reserve the cid, it might make someone able to keep someone
			//from logging in, by hogging the cid (and waiting indefinately with
			//entering HPAS))
			return;
		}

		initAction(NONE);
		Plugin::fire(Plugin::ClientLogin(), this);
		if(password.empty()) {
			login();
			state = NORMAL;
		}
	} else {
		initAction();
		Plugin::fire(Plugin::ClientInfo(), this);
		if(!getAction(STOP))
			hub->broadcast(attributes->getChangedInf());
	}
}
	
void ADCClient::handleBMSG(StringList& sl, string const& full) throw()
{
	if(sl.size() == 3) {
		initAction();
		Plugin::fire(Plugin::ClientMessage(), this, sl[2]);
		if(getAction(STOP)) {
			// do nada
		} else if(getAction(MODIFIED)) {
			hub->broadcast(esc(sl[0]) + ' ' + esc(sl[1]) + ' ' + esc(sl[2]));
		} else {
			hub->broadcast(full);
		}
	}	
}

void ADCClient::handleD(StringList& sl, string const& full) throw()
{
	// todo: check parameter count
	// "Apart from sending the message to the target, an exact copy must always be sent to the source
	//  to confirm that the hub has correctly processed the message."
	if(sl.size() >= 3 && (
			sl[0] == "DSTA" ||
			sl[0] == "DMSG" ||
			sl[0] == "DSCH" ||
			sl[0] == "DRES" ||
			sl[0] == "DCTM" ||
			sl[0] == "DRCM"
	)) {
		if(sl[0] == "DMSG" && sl[1] == hub->getCID32() && sl.size() == 5 && sl[4] == "PM") {
			send(full); 
			initAction(NONE);
			Plugin::fire(Plugin::ClientCommand(), this, sl[3]);
		} else {
			send(full); 
			hub->direct(sl[1], full);
		}
	} else {
		PROTOCOL_ERROR(sl[0] + " message type or parameter count unsupported");
	}
}
	
void ADCClient::handleH(StringList& sl, string const& full) throw()
{
	if(sl[0] == "HSUP") {
		handleHSUP(sl, full); // valid in all states
	} else if(state == VERIFY) {
		if(sl[0] == "HPAS" && sl.size() == 3) {
			handleHPAS(sl, full);
		} else {
			PROTOCOL_ERROR(sl[0] + " message type or parameter count unsupported");
		}
	} else if(state == NORMAL) {
		if(sl[0] == "HDSC") {
			handleHDSC(sl, full);
		} else if(sl[0] == "HGET" || sl[0] == "HSND") {
			doWarning("HGET/HSND unsupported");
		} else {
			PROTOCOL_ERROR(sl[0] + " message type or parameter count unsupported");
		}
	} else {
		PROTOCOL_ERROR("State mismatch");
	}
}

void ADCClient::handleHDSC(StringList& sl, string const& full) throw()
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
		ADCClient* victim = hub->getClient(victim_guid);
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
				hub->broadcast(msg);
			} else {
				if(this != victim)
					send(msg); // notify self
				hub->broadcast("IQUI " + victim_guid + " ND\n", this);
			}
		} else {
			PROTOCOL_ERROR("HDSC corrupt");
		}
	}
}
	
void ADCClient::handleHPAS(StringList& sl, string const& full) throw()
{
	// Make CID from base32
	u_int64_t cid;
	Encoder::fromBase32(guid.c_str(), (u_int8_t*)&cid, sizeof(cid));
	// Make hash
	TigerHash h;
	h.update((u_int8_t*)&cid, sizeof(cid));
	h.update(password.data(), password.length());
	h.update(salt.data(), salt.length());
	h.finalize();
	string8 hashed_pwd(h.getResult(), TigerHash::HASH_SIZE);
	string hashed_pwd_b32 = Encoder::toBase32(hashed_pwd.data(), hashed_pwd.length());
	if(hashed_pwd_b32 != sl[2]) {
		initAction(NONE);
		Plugin::fire(Plugin::ClientAuthFailed(), this, password);
		send("ISTA " + sl[1] + " 23 Bad\\ username\\ or\\ password\n");
		assert(!added);
		disconnect();
		return;
	}
	salt.clear();
	// Add user
	if(hub->hasClient(guid)) {
		PROTOCOL_ERROR("CID busy, change CID or wait");
		return;
	}
	initAction(NONE);
	Plugin::fire(Plugin::ClientAuthenticated(), this, password);
	password.clear();
	login();
	state = NORMAL;
}

void ADCClient::handleHSUP(StringList& sl, string const& full) throw()
{
	send("ISUP " + hub->getCID32() + " +BASE\n" // <-- do we need CID?
			"IINF " + hub->getCID32() + " NI" + esc(hub->getHubName()) +
			" HU1 DEmajs VEqhub0.02\n");
	state = IDENTIFY;
}
	
void ADCClient::handleP(StringList& sl, string const& full) throw()
{
	// todo: check parameter count
	if(
			sl[0] == "PMSG" ||
			sl[0] == "PSCH"
	) {
		hub->broadcastPassive(full);
	} else {
		PROTOCOL_ERROR(sl[0] + " message type or parameter count unsupported");
	}	
}
