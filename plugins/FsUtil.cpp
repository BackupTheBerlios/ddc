// vim:ts=4:sw=4:noet
#include "FsUtil.h"

#include "../ADCClient.h"
#include "../UserInfo.h"
#include "../UserData.h"
#include "../XmlTok.h"
#include "../Logs.h"
#include "../Settings.h"
#include "VirtualFs.h"

using namespace qhub;

/*
 * Plugin loader
 */

extern "C" {
	void* getPlugin() { return new FsUtil(); }
} //extern "C"



/*
 * Plugin details
 */

UserData::key_type FsUtil::idVirtualFs = "virtualfs";

bool FsUtil::load() throw()
{
	aliases.clear(); // clean old data
	XmlTok* p = Settings::getConfig("fsutil");
	p->clear();
	if(p->findChild("aliases")) {
		p = p->getNextChild();
		aliasPrefix = p->getAttr("prefix");
		if(aliasPrefix.empty())
			aliasPrefix = "+";
		if(p->findChild("alias")) {
			XmlTok* tmp;
			while((tmp = p->getNextChild())) {
				aliases[tmp->getAttr("in")] = tmp->getAttr("out");
			}
		}
		p = p->getParent();
	}
	return true;
}

bool FsUtil::save() const throw()
{
	XmlTok* p = Settings::getConfig("fsutil");
	p = p->addChild("aliases");
	p->setAttr("prefix", aliasPrefix);
	for(Aliases::const_iterator i = aliases.begin(); i != aliases.end(); ++i) {
		XmlTok* tmp = p->addChild("alias");
		tmp->setAttr("in", i->first);
		tmp->setAttr("out", i->second);
	}
	return true;
}

void FsUtil::initVFS() throw()
{
	assert(virtualfs->mkdir("/fsutil", this));
	assert(virtualfs->mknod("/fsutil/alias", this));
	assert(virtualfs->mknod("/fsutil/unalias", this));
}

void FsUtil::deinitVFS() throw()
{
	assert(virtualfs->rmdir("/fsutil"));
}

void FsUtil::on(PluginStarted&, Plugin* p) throw()
{
	if(p == this) {
		load();
		virtualfs = (VirtualFs*)Util::data.getVoidPtr(idVirtualFs);
		if(virtualfs) {
			Logs::stat << "success: Plugin FsUtil: VirtualFs interface found.\n";
			initVFS();
		} else {
			Logs::err << "warning: Plugin FsUtil: VirtualFs interface not found.\n";
		}
		Logs::stat << "success: Plugin FsUtil: Started.\n";
	} else if(!virtualfs) {
		virtualfs = (VirtualFs*)Util::data.getVoidPtr(idVirtualFs);
		if(virtualfs) {
			Logs::stat << "success: Plugin FsUtil: VirtualFs interface found.\n";
			initVFS();
		}
	}
}

void FsUtil::on(PluginStopped&, Plugin* p) throw()
{
	if(p == this) {
		if(virtualfs)
			deinitVFS();
		save();
		Logs::stat << "success: Plugin FsUtil: Stopped.\n";
	} else if(virtualfs && p == virtualfs) {
		Logs::err << "warning: Plugin FsUtil: VirtualFs interface disabled.\n";
		virtualfs = NULL;
	}
}

void FsUtil::on(PluginMessage&, Plugin* p, void* d) throw()
{
	if(virtualfs && p == virtualfs) {
		VirtualFs::Message* m = (VirtualFs::Message*)d;
		assert(m);
		if(m->type == VirtualFs::Message::CHDIR) {
			m->reply("This is the file system utilities section.");
		} else if(m->type == VirtualFs::Message::HELP) {
			if(m->cwd == "/fsutil/") {
				m->reply(
				    "The following commands are available to you:\n"
				    "load\t\t\tloads settings\n"
				    "save\t\t\tsaves settings\n"
				    "alias [alias] [command]\tlist/add aliases\n"
				    "unalias <alias>\t\tremoves an alias"
				);
			}
		} else if(m->type == VirtualFs::Message::EXEC) {
			assert(m->arg.size() >= 1);
			if(m->arg[0] == "load") {
				if(load()) {
					m->reply("Success: FsUtil settings reloaded.");
				} else {
					m->reply("Failure: Failed to reload FsUtil settings file.");
				}
			} else if(m->arg[0] == "save") {
				if(save()) {
					m->reply("Success: FsUtil settings file saved.");
				} else {
					m->reply("Failure: Failed to save FsUtil settings file.");
				}
			} else if(m->arg[0] == "alias") {
				if(m->arg.size() == 1) {
					string tmp = "Success: aliases, prefix = \"" + aliasPrefix + "\":\r\n";
					for(Aliases::const_iterator i = aliases.begin(); i != aliases.end(); ++i) {
						tmp += i->first + " = " + i->second + "\r\n";
					}
					m->reply(tmp);
				} else if(m->arg.size() == 2) {
					Aliases::const_iterator i = aliases.find(m->arg[1]);
					if(i != aliases.end())
						m->reply("Success: " + i->first + " = " + i->second);
					else
						m->reply("Failed: " + m->arg[1] + " undefined");
				} else if(m->arg.size() == 3) {
					aliases[m->arg[1]] = m->arg[2];
					m->reply("Success: alias added/modified");
				} else {
					m->reply("Syntax: alias [alias] [command]");
				}
			} else if(m->arg[0] == "unalias") {
				if(m->arg.size() == 2) {
					Aliases::iterator i = aliases.find(m->arg[1]);
					if(i != aliases.end()) {
						aliases.erase(i);
						m->reply("Success: alias removed");
					} else {
						m->reply("Failed: no such alias defined");
					}
				} else {
					m->reply("Syntax: del <nick>");
				}
			}
		}
	}
}

void FsUtil::on(UserCommand& a, ADCClient* client, string& msg) throw()
{
	if(msg.compare(0, aliasPrefix.length(), aliasPrefix) == 0) {
		string::size_type i = msg.find(' ');
		if(i == string::npos)
			i = msg.length();
		Aliases::const_iterator j = aliases.find(msg.substr(aliasPrefix.length(), i - aliasPrefix.length()));
		if(j != aliases.end()) {
			msg.replace(0, i, j->second);
			a.setState(Plugin::MODIFIED);
		}
	}
}

void FsUtil::on(UserMessage& a, ADCClient* c, u_int32_t const cmd, string& msg) throw()
{
	if(msg.compare(0, aliasPrefix.length(), aliasPrefix) == 0) {
		string::size_type i = msg.find(' ');
		if(i == string::npos)
			i = msg.length();
		Aliases::const_iterator j = aliases.find(msg.substr(aliasPrefix.length(), i - aliasPrefix.length()));
		if(j != aliases.end()) {
			msg.replace(0, i, j->second);
			Plugin::UserCommand action;
			Plugin::fire(action, c, msg);
			a.setState(Plugin::STOP);
		}
	}
}

void FsUtil::on(UserPrivateMessage& a, ADCClient* c, u_int32_t const cmd, string& msg, string& pm) throw()
{
	if(msg.compare(0, aliasPrefix.length(), aliasPrefix) == 0) {
		string::size_type i = msg.find(' ');
		if(i == string::npos)
			i = msg.length();
		Aliases::const_iterator j = aliases.find(msg.substr(aliasPrefix.length(), i - aliasPrefix.length()));
		if(j != aliases.end()) {
			msg.replace(0, i, j->second);
			Plugin::UserCommand action;
			Plugin::fire(action, c, msg);
			a.setState(Plugin::STOP);
		}
	}
}
