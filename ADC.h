// vim:ts=4:sw=4:noet
#ifndef _INCLUDED_ADC_H_
#define _INCLUDED_ADC_H_

#include "ADCSocket.h"
#include "compat_hash_map.h"
#include <vector>
#include <string>
#include <queue>

#include <boost/shared_ptr.hpp>

#include "Buffer.h"
#include "string8.h"

namespace qhub {

using namespace std;

class Hub;
class ADCInf;

class ADC : public ADCSocket {
public:
	enum State {
		START,		// HSUP
		IDENTIFY,	// BINF
		VERIFY,		// HPAS
		NORMAL,
		DISCONNECTED	// signals that one shouldn't use this anymore
	};
	
	ADC(int fd, Hub* parent);
	virtual ~ADC();

	virtual void on_read() { ADCSocket::on_read(); };
	virtual void on_write() { ADCSocket::on_write(); };

	// Other stuff
	string const& getInf() const;
	string const& getCID32() const { return guid; };
	ADCInf* getAttr() { return attributes; };

	/*
	 * Object information
	 */
	State getState() const throw() { return state; };
	void setInt(string const& key, int value) throw();
	int getInt(string const& key) throw();
	// add setVoid/setInt64/setString..

	/*
	 * Various calls (don't send in bad states!)
	 */
	virtual void doAskPassword(string const& pwd) throw(); // send at LOGIN only!
	virtual void doWarning(string const& msg) throw();
	virtual void doError(string const& msg) throw();
	virtual void doDisconnect(string const& msg = Util::emptyString) throw();
	virtual void doHubMessage(string const& msg) throw();

protected:
	/*
	 * Calls from ADCSocket
	 */
	virtual void onLine(StringList const& sl, string const& full) throw();
	virtual void onConnected() throw();
	virtual void onDisconnected(string const& clue) throw();
	
private:
	/*
	 * Data handlers	
	 */
	void handleA(StringList const& sl, string const& full);
	void handleB(StringList const& sl, string const& full);
	void handleBINF(StringList const& sl, string const& full);
	void handleBMSG(StringList const& sl, string const& full);
	void handleD(StringList const& sl, string const& full);
	void handleH(StringList const& sl, string const& full);
	void handleHDSC(StringList const& sl, string const& full);
	void handleHPAS(StringList const& sl, string const& full);
	void handleHSUP(StringList const& sl, string const& full);
	void handleP(StringList const& sl, string const& full);

	void login();
	void logout();
	bool added;

	ADCInf* attributes;	
	Hub* hub;

	State state;
	string guid;
	string password;
	string8 salt;

	typedef hash_map<string, int> IntMap; // string is probably _way_ too slow, fixme..
	IntMap intMap;
	
	// Invalid
	ADC() : ADCSocket(-1), attributes(0) {};
};

}

#endif //_INCLUDED_ADC_H_
