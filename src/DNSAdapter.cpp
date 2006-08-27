// vim:ts=4:sw=4:noet

#include "DNSAdapter.h"
#include "Logs.h"

#include <sys/types.h>

#ifdef WIN32
#else
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <ares.h>

using namespace std;
using namespace qhub;

static void callback(void *arg, int status, struct hostent *host);

DNSAdapter::DNSAdapter(const string& hostname) : query(hostname), fd(-1)
{
	int status = ares_init(&channel);
	
    if (status != ARES_SUCCESS)
	{
		Logs::stat << "ares_init: " << ares_strerror(status) << endl;
	}

	struct in_addr addr;
	
	addr.s_addr = inet_addr(query.c_str());
	//XXX: looking ip numeric ips -> hostname seems to corrupt memory
	if (addr.s_addr == INADDR_NONE) {
		ares_gethostbyname(channel, query.c_str(), AF_INET, callback, this);
	} else {
	    ares_gethostbyaddr(channel, &addr, sizeof(struct in_addr), AF_INET, callback, this);
	}

	doHack();
}

DNSAdapter::~DNSAdapter() throw()
{
	Logs::stat << "TIMEOUT!" << endl;
	ares_destroy(channel);
	EventManager::instance()->disableRead(fd);
	EventManager::instance()->disableWrite(fd);
}

// not only hackish, will probably cause nasty segfaults as we can't remove
// fd from event listeners
// FIXME XXX TODO   -- yes, it's that bad
bool DNSAdapter::doHack()
{
	fd_set read_fds, write_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	int nfds = ares_fds(channel, &read_fds, &write_fds);
	
	assert(nfds<FD_SETSIZE && "FD_SETSIZE is too small for us.");
	
	if(nfds != 0){
		fd = nfds-1;
		if(FD_ISSET(fd, &read_fds))
			EventManager::instance()->enableRead(fd, this);
		if(FD_ISSET(fd, &write_fds))
			EventManager::instance()->enableWrite(fd, this);
	}

	return true;
}

void DNSAdapter::onTimer(int) throw()
{
	delete this;
}

void DNSAdapter::onRead(int) throw()
{
	fd_set read_fds, write_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	FD_SET(fd, &read_fds);

	ares_process(channel, &read_fds, &write_fds);
	
	doHack();
}

void DNSAdapter::onWrite(int) throw()
{
	fd_set read_fds, write_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	FD_SET(fd, &write_fds);

	ares_process(channel, &read_fds, &write_fds);
	
	doHack();
}


void DNSAdapter::init()
{
}

static void callback(void *arg, int status, struct hostent *host)
{
	DNSAdapter *us = (DNSAdapter*) arg;
	
	struct in_addr addr;
	char **p;

	if (status != ARES_SUCCESS) {
		Logs::err << "Error: " << ares_strerror(status) << endl;
		return;
	}

	for (p = host->h_addr_list; *p; p++) {
		memcpy(&addr, *p, sizeof(struct in_addr));
		char *textual = inet_ntoa(addr);
		//printf("%-32s\t%s\n", host->h_name, textual);
		if(textual) {
			us->complete(inet_ntoa(addr));
		} else {
			us->complete(host->h_name);
		}
	}
}