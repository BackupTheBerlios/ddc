#ifndef __HUB_H_
#define __HUB_H_

#include "compat_hash_map.h"

#include <list>
#include <string>
#include <boost/shared_ptr.hpp>

#include "Buffer.h"
#include "DNSUser.h"
#include "InterHub.h"

using namespace std;

namespace qhub {

class ADC;

class Hub : public DNSUser {
public:
	Hub();

	void openADCPort(int port);
	void openInterPort(int port);
	void openInterConnection(string host, int port, string password);
	void onLookup(adns_answer *reply) const;

	void setHubName(string name) { Hub::name=name; };
	string getHubName() { return name; }

	void setMaxPacketSize(int s);
	int getMaxPacketSize() { return maxPacketSize; }

	void acceptLeaf(int fd);
	void acceptInterHub(int fd);

	bool addClient(ADC* client, string guid);
	void removeClient(string guid);

	void broadcast(ADC* c, string data);
	void broadcastSelf(ADC* c, string data);
	void direct(string guid, string data);

	void motd(ADC* c);

	void getUsersList(ADC* c);
private:
	string name;

	int maxPacketSize;

	//this is for external users

	//this is for physical users
	hash_map<string, ADC*> users;
	typedef hash_map<string, ADC*>::iterator userIter;

	//userlist-cache.
	qhub::Buffer::writeBuffer userlist;
	list<qhub::Buffer::writeBuffer> outliers;

	//opened connections
	hash_map<string, InterHub*> interConnects;

	//listened opened connections
	list<InterHub*>	interConnects2;

	void createCache();
};

}

#endif
