// vim:ts=4:sw=4:noet
#include <cassert>
#include <iostream>
#include <boost/program_options.hpp>

#include "error.h"

#include "Settings.h"
#include "Hub.h"
#include "XmlTok.h"
#include "Util.h"
#include "Logs.h"
#include "PluginManager.h"

using namespace qhub;
using namespace std;

XmlTok* Settings::getConfig(const string& name) throw()
{
	XmlTok* p = (root.findChild("qhub"), root.getNextChild());
	assert(p);
	if(p->findChild(name))
		return p->getNextChild();
	else 
		return p->addChild(name);
}

bool Settings::isValid() throw()
{
	XmlTok* p;
	return root.findChild("qhub")
	    && (p = root.getNextChild())
	    && p->findChild("__connections")
	    && p->findChild("__hub")
//	    && (p = p->getNextChild())
//	    && ADC::checkCID(p->getAttr("cid")) // enough checks for now :)
	    ;
}

void Settings::load() throw()
{
	if(!root.load(CONFIGDIR "/qhub.xml") || !isValid()) {
		Logs::err << "Config file missing or corrupt, entering interactive setup\n";
		loadInteractive();
	}
}

void Settings::loadInteractive() throw()
{
	string name, interpass, prefix;
	vector<u_int16_t> cports, iports;
	cout << "Hub name: ";
	getline(cin, name);
	cout << "Client ports (0 when done): ";
	while(cin) {
		int port;
		cin >> port;
		if(!port)
			break;
		cports.push_back(port);
	}
	cout << "Interconnect ports (0 when done): ";
	while(cin) {
		int port;
		cin >> port;
		if(!port)
			break;
		iports.push_back(port);
	}
	if(!iports.empty()) {
		cout << "Interconnect password: ";
		cin >> interpass;
	}
	cout << "number of bits to be used for hub identification on this network "
			<< "(0 if this hub will not be part of a network): ";
	int bits;
	cin >> bits;
	int id = 0;
	while(bits != 0) {
		cout << "hub id number for the network (0 for no network): ";
		cin >> id;
		if(id < (1 << bits))
			break;
		cerr << "hub id is too large for number of bits!" << endl;
	}

	root.clear();
	XmlTok* p = root.addChild("qhub");
	p = p->addChild("__hub");
	p->setAttr("name", name);
	p->setAttr("hubsidbits", Util::toString(bits));
	p->setAttr("sid", Util::toString(id << (20 - bits)));
	if(!interpass.empty())
		p->setAttr("interpass", interpass);
	p = p->getParent();
	p = p->addChild("__connections");
	for(vector<u_int16_t>::iterator i = cports.begin(); i != cports.end(); ++i)
		p->addChild("clientport")->setData(Util::toString(*i));
	for(vector<u_int16_t>::iterator i = iports.begin(); i != iports.end(); ++i)
		p->addChild("interport")->setData(Util::toString(*i));
}

void Settings::save() throw()
{
	if(!root.save(CONFIGDIR "/qhub.xml"))
		Logs::err << "Unable to save config file, check write permissions" << endl;
}

static const char*const _version_str =
PACKAGE_NAME "/" PACKAGE_VERSION " built " __DATE__ "\n"
"Written by Walter Doekes (Sedulus),\n"
"\tJohn Backstrand (sandos), and Matt Pearson (Pseudo)\n"
"Homepage: http://ddc.berlios.de\n"
"SVN:      svn://svn.berlios.de/ddc/qhub\n";

void Settings::parseArgs(int argc, char** argv)
{
	using namespace boost::program_options;

	options_description desc("Options");
	desc.add_options()
		("help", "display this help and exit")
		("version", "display version information and exit")
		("statfile", value<string>(), "status messages logged to 'arg' (default stdout)")
		("errfile", value<string>(), "error messages logged to 'arg' (default stderr)")
#ifdef DEBUG
		("linefile", value<string>(), "protocol lines logged to 'arg'")
		("verbose,v", "protocol lines logged to stdout (only for debugging)")
#endif
		("plugin,p", value<StringList>(), "load plugin 'arg'")
		("daemonize,d", "run as daemon")
		("quiet,q", "no output");

	variables_map vm;
	store(parse_command_line(argc, argv, desc), vm);
	notify(vm);

	if(vm.count("help")) {
		cout << desc;
		exit(EXIT_SUCCESS);
	}
	if(vm.count("version")) {
		cout << _version_str;
		exit(EXIT_SUCCESS);
	}
#ifdef DEBUG
	if(vm.count("verbose"))
		Logs::copy(Logs::stat, Logs::line);
#endif
	if(vm.count("statfile"))
		Logs::setStat(vm["statfile"].as<string>());
#ifdef DEBUG
	if(vm.count("linefile"))
		Logs::setLine(vm["statfile"].as<string>());
#endif
	if(vm.count("errfile"))
		Logs::setErr(vm["errfile"].as<string>());
	if(vm.count("quiet")) {
#ifdef DEBUG
		Logs::line.rdbuf(0);
#endif
		Logs::stat.rdbuf(0);
		Logs::err.rdbuf(0);
	}
	if(vm.count("plugin")) {
		const StringList& p = vm["plugin"].as<StringList>();
		for(StringList::const_iterator i = p.begin(); i != p.end(); ++i)
			PluginManager::instance()->open(*i);
	}
	if(vm.count("daemonize"))
		// TODO check to make sure config is done and valid
		Util::daemonize();
}
