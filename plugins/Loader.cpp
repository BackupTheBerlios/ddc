// vim:ts=4:sw=4:noet
#include "Loader.h"
#include "VirtualFs.h"
#include "../XmlTok.h"
#include "../Util.h"
#include "../Logs.h"
#include "../Settings.h"

using namespace qhub;

/*
 * Plugin loader
 */

extern "C" {
	void* getPlugin() { return new Loader(); }
} //extern "C"



/*
 * Plugin details
 */

UserData::key_type Loader::idVirtualFs = "virtualfs";

int Loader::load() throw()
{
	int success = 0;
	int failure = 0;
	XmlTok* p = Settings::getConfig("plugins");
	if(p->findChild("plugin")) {
		XmlTok* tmp;
		while((tmp = p->getNextChild())) {
			string const& name = tmp->getData();
			if(!Plugin::hasModule(name) && name != "loader") { // we're not added yet! don't want inf-recurse
				Logs::stat << "\nloading plugin \"" << name << "\"\n";
				if(Plugin::openModule(name)) {
					success++;
				} else {
					failure++;
				}
			}
		}
	} else {
		return 0;
	}
	return failure == 0 ? success : -failure;
}

bool Loader::save() const throw()
{
	XmlTok root;
	XmlTok* p = Settings::getConfig("plugins");
	p->clear();
	for(Plugin::iterator i = Plugin::begin(); i != Plugin::end(); ++i) {
		XmlTok* tmp = p->addChild("plugin");
		tmp->setData((*i)->getId());
	}
	return true;
}

void Loader::initVFS() throw()
{
	assert(virtualfs->mkdir("/plugins", this));
	assert(virtualfs->mknod("/plugins/load", this));
	assert(virtualfs->mknod("/plugins/save", this));
	assert(virtualfs->mknod("/plugins/list", this));
	assert(virtualfs->mknod("/plugins/insert", this));
	assert(virtualfs->mknod("/plugins/remove", this));
	assert(virtualfs->mknod("/plugins/removeall", this));
	assert(virtualfs->mknod("/plugins/restart", this));
}

void Loader::deinitVFS() throw()
{
	assert(virtualfs->rmdir("/plugins"));
}

void Loader::on(PluginStarted&, Plugin* p) throw()
{
	if(p == this) {
		load();
		virtualfs = (VirtualFs*)Util::data.getVoidPtr(idVirtualFs);
		if(virtualfs) {
			Logs::stat << "success: Plugin Loader: VirtualFs interface found.\n";
			initVFS();
		} else {
			Logs::err << "warning: Plugin Loader: VirtualFs interface not found.\n";
		}
		Logs::stat << "success: Plugin Loader: Started.\n";
	} else if(!virtualfs) {
		virtualfs = (VirtualFs*)Util::data.getVoidPtr(idVirtualFs);
		if(virtualfs) {
			Logs::stat << "success: Plugin Loader: VirtualFs interface found.\n";
			initVFS();
		}
	}
}

void Loader::on(PluginStopped&, Plugin* p) throw()
{
	if(p == this) {
		if(virtualfs)
			deinitVFS();
		Logs::stat << "success: Plugin Loader: Stopped.\n";
	} else if(virtualfs && p == virtualfs) {
		Logs::err << "warning: Plugin Loader: VirtualFs interface disabled.\n";
		virtualfs = NULL;
	}
}

void Loader::on(PluginMessage&, Plugin* p, void* d) throw()
{
	if(virtualfs && p == virtualfs) {
		VirtualFs::Message* m = (VirtualFs::Message*)d;
		assert(m);
		if(m->type == VirtualFs::Message::CHDIR) {
			m->reply("This is the plugins section. Load and unload plugins here.");
		} else if(m->type == VirtualFs::Message::HELP) {
			if(m->cwd == "/plugins/") {
				m->reply(
				    "The following commands are available to you:\r\n"
				    "load\t\t\tloads the plugins in the settings file (done automatically at startup)\r\n"
				    "save\t\t\tsaves the plugin load order to disk (must be done manually)\r\n"
				    "list\t\t\tshows the list of loaded plugins\r\n"
				    "insert <plugin> [insertbefore]\tloads the plugin in the specified position\r\n"
				    "remove <plugin>\t\tunloads the plugin\r\n"
				    "removeall\t\t\tunloads every plugin except this one and virtualfs\r\n"
				    "restart <plugin>\t\tunloads and loads the plugin (e.g. to reload settings)\r\n"
				);
			}
		} else if(m->type == VirtualFs::Message::EXEC) {
			assert(m->arg.size() >= 1);
			if(m->arg[0] == "load") {
				int n = 0;
				if((n = load()) >= 0) {
					m->reply("Success: " + Util::toString(n) + " plugin(s) loaded from disk.");
				} else {
					m->reply("Failure: " + Util::toString(-n) + " plugin(s) failed to load.");
				}
			} else if(m->arg[0] == "save") {
				if(save()) {
					m->reply("Success: Plugin load order saved to disk.");
				} else {
					m->reply("Failure: Saving plugin load order to disk failed.");
				}
			} else if(m->arg[0] == "list") {
				string ret = "Success: Plugins loader:\r\n";
				for(Plugin::iterator i = Plugin::begin(); i != Plugin::end(); ++i) {
					ret += (*i)->getId() + ((*i)->getId() == "virtualfs" ? " (cannot be unloaded)\r\n" : "\r\n");
				}
				m->reply(ret);
			} else if(m->arg[0] == "insert") {
				if(m->arg.size() == 3 && !hasModule(m->arg[1]) && Plugin::openModule(m->arg[1], m->arg[2])) {
					m->reply("Success: Plugin loaded.");
				} else if(m->arg.size() == 2 && !hasModule(m->arg[1]) && Plugin::openModule(m->arg[1])) {
					m->reply("Success: Plugin loaded.");
				} else if(m->arg.size() == 2 | m->arg.size() == 3) {
					m->reply("Failure: Plugin failed to load.");
				} else {
					m->reply("Syntax: insert <plugin> [insertbefore]");
				}
			} else if(m->arg[0] == "restart") {
				if(m->arg.size() == 2) {
					Plugin::iterator j = Plugin::end();
					string next;
					for(Plugin::iterator i = Plugin::begin(); i != Plugin::end(); ++i) {
						if((*i)->getId() == m->arg[1]) {
							j = i++;
							if(i != Plugin::end())
								next = (*i)->getId();
							break;
						}
					}
					if(j != Plugin::end()) {
						Plugin::removeModule(m->arg[1]);
						if(Plugin::openModule(m->arg[1], next)) {
							m->reply("Success: Plugin restarted.");
						} else {
							m->reply("Failure: Plugin failed to restart properly.");
						}
					} else {
						m->reply("Failure: Plugin not loaded.");
					}
				} else {
					m->reply("Syntax: restart <plugin>");
				}
			} else if(m->arg[0] == "removeall") {
				string ret = "Success: Unloaded the following plugins:\r\n";
				while(true) {
					Plugin::iterator i; // Iterator is invalidated on removeModule
					for(i = Plugin::begin(); i != Plugin::end(); ++i) {
						// Don't unload ourself and dependencies
						if(*i == this || (virtualfs && *i == virtualfs))
							continue;
						ret += (*i)->getId() + "\r\n";
						Plugin::removeModule((*i)->getId());
						break;
					}
					if(i == Plugin::end())
						break;
				}
				m->reply(ret);
			} else if(m->arg[0] == "remove") {
				string ret = "Success: Unloaded the following plugins:\r\n";
				if(m->arg.size() != 1) {
					for(unsigned i = 1; i < m->arg.size(); ++i) {
						// can't remove virtualfs, we're being called by it
						if(m->arg[i] != "virtualfs" && Plugin::removeModule(m->arg[i])) {
							ret += m->arg[i] + "\r\n";
						}
					}
					m->reply(ret);
				} else {
					m->reply("Syntax: remove <plugin1> ... <pluginN>");
				}
			}
		}
	}
}

