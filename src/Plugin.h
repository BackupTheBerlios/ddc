// vim:ts=4:sw=4:noet
#ifndef QHUB_PLUGIN_H
#define QHUB_PLUGIN_H

#include "qhub.h"

#include <cassert>
#include <string>
#include <vector>

namespace qhub {

class Plugin {
	/*
	 * Some stuff
	 */
protected:
	explicit Plugin(const char* n) throw() : name(n) {}
public:
	virtual ~Plugin() throw() {}
	std::string const& getId() const throw() { return name; }

	/*
	 * Plugin calls
	 */
	enum {
		// Plugin messages
		PLUGIN_STARTED,
		PLUGIN_STOPPED,
		PLUGIN_MESSAGE,
		// Client messages
		CLIENT_CONNECTED,		// client connected (not everything may be initialized??)
		CLIENT_DISCONNECTED,	// client disconnected
		CLIENT_LINE,			// client send data
		CLIENT_LOGIN,			// client tries to authenticate with first BINF
		CLIENT_INFO,			// client has sent a BINF (not called on first BINF)
		// Logged in client messages
		USER_CONNECTED,			// user connected / logged in properly
		USER_DISCONNECTED,		// user disconnected
		USER_COMMAND,			// user sends private message to hub bot
		USER_MESSAGE,			// user sends message
		USER_PRIVATEMESSAGE,	// user sends message with PM flag
		// Interhub messages
		INTER_CONNECTED,
		INTER_DISCONNECTED,
		INTER_LINE,
		// Last
		LAST
	};
	// add AUTH_FAILED
	//
	// FIXME add someway to disable stringlist editing when not in CLIENT_LINE

	enum Action {
		NOTHING = 0x0,
		MODIFY = 0x1,
		MODIFIED = MODIFY, // plugin has modified the input
		HANDLE = 0x2,
		HANDLED = HANDLE, // plugin has done something (other plugins may choose to do nothing now)
		STOP = 0x4,
		STOPPED = STOP, // plugin requests that hub does not process the command/message
		DISCONNECT = 0x8,
		DISCONNECTED = DISCONNECT // client has been removed, do as little as possible
	};

	template<int I, int C> struct ActionType {
		enum { actionType = I };
		ActionType() throw() : can(C), does(0) { };
		~ActionType() throw() { };
		void setState(Action d) throw() { assert((can & d) == d); does |= d; };
		bool isSet(Action d) const throw() { return (does & d) == d; }
	private:
		int can;
		int does;
	};

	typedef ActionType<PLUGIN_STARTED, NOTHING>
			PluginStarted;
	typedef ActionType<PLUGIN_STOPPED, NOTHING>
			PluginStopped;
	typedef ActionType<PLUGIN_MESSAGE, HANDLE>
			PluginMessage;

	typedef ActionType<CLIENT_CONNECTED, HANDLE | DISCONNECT>
			ClientConnected;
	typedef ActionType<CLIENT_DISCONNECTED, HANDLE>
			ClientDisconnected;
	typedef ActionType<CLIENT_LINE, MODIFY | HANDLE | STOP | DISCONNECT>
			ClientLine;
	typedef ActionType<CLIENT_LOGIN, HANDLE | DISCONNECT>
			ClientLogin;
	typedef ActionType<CLIENT_INFO, HANDLE | DISCONNECT | MODIFY>
			ClientInfo;

	typedef ActionType<USER_CONNECTED, HANDLE | DISCONNECT>
			UserConnected;
	typedef ActionType<USER_DISCONNECTED, HANDLE>
			UserDisconnected;
	typedef ActionType<USER_COMMAND, MODIFY | HANDLE | STOP | DISCONNECT>
			UserCommand;
	typedef ActionType<USER_MESSAGE, HANDLE | STOP | DISCONNECT>
			UserMessage;
	typedef ActionType<USER_PRIVATEMESSAGE, HANDLE | STOP | DISCONNECT>
			UserPrivateMessage;

	typedef ActionType<INTER_CONNECTED, HANDLE | DISCONNECT>
			InterConnected;
	typedef ActionType<INTER_DISCONNECTED, HANDLE>
			InterDisconnected;
	typedef ActionType<INTER_LINE, HANDLE | DISCONNECT | MODIFY | STOP>
			InterLine;

	// Called after construction
	// parm: Plugin* = the plugin
	virtual void on(PluginStarted&, Plugin*) throw() {};
	// Called before destruction
	// parm: Plugin* = the plugin
	virtual void on(PluginStopped&, Plugin*) throw() {};
	// Called by a plugin
	// parm: Plugin* = the plugin
	// parm: void* = the custom data
	virtual void on(PluginMessage&, Plugin*, void*) throw() {};
	// Called when a client connects
	// note: not everything may be initialized
	// parm: Client* = the client
	virtual void on(ClientConnected&, Client*) throw() {};
	// Called when a client disconnects
	// note: the client may not have been added to the userlist
	// parm: Client* = the client
	virtual void on(ClientDisconnected&, Client*) throw() {};
	// Called on every client input
	// parm: Client* = the client
	// parm: Command&: command that was sent
	virtual void on(ClientLine&, Client*, Command&) throw() {};
	// Called when a client sends first BINF
	// parm: Client* = the client
	virtual void on(ClientLogin&, Client*) throw() {};
	// Called when a client sends BINF
	// parm: Client* = the client
	// parm: UserInfo = the new userinfo
	virtual void on(ClientInfo&, Client*, UserInfo&) throw() {};
	// Called when client succesfully logs in
	// parm: Client* = the client
	virtual void on(UserConnected&, Client*) throw() {};
	// Called when a logged in client logs off
	// parm: Client* = the client
	virtual void on(UserDisconnected&, Client*) throw() {};
	// Called when a client sends a direct PM to the hub CID
	// parm: Client* = the client
	// parm: string = the message
	virtual void on(UserCommand&, Client*, const std::string&) throw() {};
	// Called when a client sends a normal broadcast chat message
	// parm: Client* = the client
	// parm: string = the message
	virtual void on(UserMessage&, Client*, Command&, std::string&) throw() {};
	virtual void on(UserPrivateMessage&, Client*, Command&, std::string&, sid_type) throw() {};
	// Called when another hub connects
	// parm: InterHub* = the interhub connection
	virtual void on(InterConnected&, InterHub*) throw() {};
	// Called when hub disconnects
	// parm: InterHub* = the interhub connection
	virtual void on(InterDisconnected&, InterHub*) throw() {};
	// Called on interhub input
	// parm: InterHub* = the interhub connection
	// parm: Command& = command that was sent
	virtual void on(InterLine&, InterHub*, Command&) throw() {};

private:
	friend class PluginManager;
	const std::string name;
	void* handle;
};

/*
 * macro for defining the function used to retrieve plugin from shared library.
 * Also has function to delete, because it's generally a bad idea to create and
 * delete objects in different modules, due to possibly differing memory
 * allocators (read this somewhere, but don't remember where).
 *
 * Also, just because we have C linkage doesn't mean we can't use C++ types.
 */
#define QHUB_PLUGIN(Name) \
extern "C" Plugin* getPlugin() { return new Name; } \
extern "C" void putPlugin(Plugin* p) { delete p; }

} //namespace qhub

#endif //QHUB_PLUGIN_H
