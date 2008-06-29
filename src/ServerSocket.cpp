// vim:ts=4:sw=4:noet
#include "ServerSocket.h"

#include "ConnectionManager.h"
#include "Logs.h"

using namespace std;
using namespace qhub;

ServerSocket::ServerSocket(Domain domain, int port, int t) : Socket(domain), type(t)
{
	int yes = 1;

	if(setsockopt(getFd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		Logs::err << "warning: setsockopt:SO_REUSEADDR: " << Util::errnoToString(errno) << endl;
	}

	bind(Util::emptyString, port);	// empty = use INADDR_ANY
	listen();

	EventManager::instance()->enableRead(getFd(), this);
}

ServerSocket::~ServerSocket() throw()
{
	if(getFd() != -1)
		close(getFd());
}

void ServerSocket::onRead(int) throw()
{
	while(true){
		int fd;
		Domain d;
		Socket::accept(fd, d);
		if(fd != -1){
			switch(type){
			case INTER_HUB:
				Logs::stat << "accepted ihub socket " << fd << endl;
				ConnectionManager::instance()->acceptInterHub(fd, d);
				break;
			case LEAF_HANDLER:
				Logs::stat << "accepted leaf socket " << fd << endl;
				ConnectionManager::instance()->acceptLeaf(fd, d);
				break;
			default:
				assert(0 && "unknown type for listening socket.");
			}
		} else {
			break;
		}
	}
}

void ServerSocket::onWrite(int) throw()
{
	assert(0 && "ServerSocket received a write.");
}

