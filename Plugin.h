#ifndef __PLUGIN_H_
#define __PLUGIN_H_

#include <string>
#include <ltdl.h>

#include <compat_hash_map.h>
#include <list>

using namespace std;

namespace qhub {

class ADC;

class Plugin {
public:
	static void init();
	static void deinit();

	//adds or removes modules from the module-list
	static void openModule(const char* filename);
	static void removeModule(const char* filename);

	static void onLogin(ADC* client);

	Plugin(const char* name, const lt_dlhandle h);
	virtual ~Plugin();

	const char* getName() { return name.c_str(); };

private:
	//loads methods from our module
	void loadFromModule();
	void load(const char* name);

	static list<Plugin*> modules;

	/* Per-class stuff
	 * 
	 */

	//our handle, name
	string name;
	lt_dlhandle handle;

	//all our functions
	//this should be replaces by function pointers (performance)
	hash_map<string, lt_ptr> functions;
};

}

#endif
