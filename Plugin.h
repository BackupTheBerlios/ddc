// vim:ts=4:sw=4:noet
#ifndef _INCLUDED_PLUGIN_H_
#define _INCLUDED_PLUGIN_H_

#include <string>
#include <cassert>
#include <stdint.h>

#include "compat_hash_map.h"
#include <list>

#include "Util.h"
#include "UserData.h"

using namespace std;

namespace qhub {

class ADCClient;
class UserInfo;
class InterHub;

class Plugin {
public:
	static const string PLUGIN_EXTENSION;
	
	/*
	 * Some stuff
	 */
	Plugin() throw() {};
	virtual ~Plugin() throw() {};
	string const& getId() const throw() { return name; };

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

	
	template<typename T0>
	static void fire(T0& type) throw() {
		for(Plugins::iterator i = plugins.begin(); i != plugins.end(); ++i) {
			(*i)->on(type);
		}
	}
	template<typename T0, class T1>
	static void fire(T0& type, T1& c1) throw() {
		for(Plugins::iterator i = plugins.begin(); i != plugins.end(); ++i) {
			(*i)->on(type, c1);
		}
	}
	template<typename T0, class T1, class T2>
	static void fire(T0& type, T1& c1, T2& c2) throw() {
		for(Plugins::iterator i = plugins.begin(); i != plugins.end(); ++i) {
			(*i)->on(type, c1, c2);
		}
	}
	template<typename T0, class T1, class T2, class T3>
	static void fire(T0& type, T1& c1, T2& c2, T3& c3) throw() {
		for(Plugins::iterator i = plugins.begin(); i != plugins.end(); ++i) {
			(*i)->on(type, c1, c2, c3);
		}
	}
	template<typename T0, class T1, class T2, class T3, class T4>
	static void fire(T0& type, T1& c1, T2& c2, T3& c3, T4& c4) throw() {
		for(Plugins::iterator i = plugins.begin(); i != plugins.end(); ++i) {
			(*i)->on(type, c1, c2, c3, c4);
		}
	}
	template<typename T0, class T1, class T2, class T3, class T4, class T5>
	static void fire(T0& type, T1& c1, T2& c2, T3& c3, T4& c4, T5& c5) throw() {
		for(Plugins::iterator i = plugins.begin(); i != plugins.end(); ++i) {
			(*i)->on(type, c1, c2, c3, c4, c5);
		}
	}

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
	// parm: ADCClient* = the client
	virtual void on(ClientConnected&, ADCClient*) throw() {};
	// Called when a client disconnects
	// note: the client may not have been added to the userlist
	// parm: ADCClient* = the client
	virtual void on(ClientDisconnected&, ADCClient*) throw() {};
	// Called on every client input
	// parm: ADCClient* = the client
	// parm: uint32_t = FOURCC of command
	// parm: StringList: list of command arguments
	virtual void on(ClientLine&, ADCClient*, uint32_t const, StringList) throw() {};
	// Called when a client sends first BINF
	// parm: ADCClient* = the client
	virtual void on(ClientLogin&, ADCClient*) throw() {};
	// Called when a client sends BINF
	// parm: ADCClient* = the client
	// parm: UserInfo = the new userinfo
	virtual void on(ClientInfo&, ADCClient*, UserInfo&) throw() {};
	// Called when client succesfully logs in
	// parm: ADCClient* = the client
	virtual void on(UserConnected&, ADCClient*) throw() {};
	// Called when a logged in client logs off
	// parm: ADCClient* = the client
	virtual void on(UserDisconnected&, ADCClient*) throw() {};
	// Called when a client sends a direct PM to the hub CID
	// parm: ADCClient* = the client
	// parm: string = the message
	virtual void on(UserCommand&, ADCClient*, string&) throw() {};
	// Called when a client sends a normal broadcast chat message
	// parm: ADCClient* = the client
	// parm: string = the message
	virtual void on(UserMessage&, ADCClient*, uint32_t const, string&) throw() {};
	virtual void on(UserPrivateMessage&, ADCClient*, uint32_t const, string&, string&) throw() {};
	// Called when another hub connects
	// parm: InterHub* = the interhub connection
	virtual void on(InterConnected&, InterHub*) throw() {};
	// Called when hub disconnects
	// parm: InterHub* = the interhub connection
	virtual void on(InterDisconnected&, InterHub*) throw() {};
	// Called on interhub input
	// parm: InterHub* = the interhub connection
	// parm: uint32_t = FOURCC of command
	// parm: StringList = list of command arguments
	virtual void on(InterLine&, InterHub*, const uint32_t, StringList&) throw() {};

	/*
	 * Static methods
	 */
	static void init() throw();
	static void deinit() throw();
	static bool openModule(string const& name, string const& insertBefore = Util::emptyString) throw();
	static bool removeModule(string const& name) throw();
	static void removeAllModules() throw();
	static bool hasModule(string const& name) throw();

	/*
	 * Iterators
	 */
	typedef list<Plugin*>::iterator iterator;
	static iterator begin() throw() { return plugins.begin(); };
	static iterator end() throw() { return plugins.end(); };

private:
	typedef list<Plugin*> Plugins;
	static Plugins plugins;
	typedef void* (*get_plugin_t)();
	string name;
	void* handle;
};

} //namespace qhub

#endif //_INCLUDED_PLUGIN_H_
