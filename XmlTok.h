// vim:ts=4:sw=4:noet
#ifndef _INCLUDED_XMLTOK_H_
#define _INCLUDED_XMLTOK_H_

#include <vector>
#include <string>
#include <utility>
#include "Util.h"

using namespace std;

namespace qhub {

class XmlTok {
public:
	static char indentChar;
	/*
	 * Getters
	 */
	XmlTok(string const& name = "", XmlTok* parent = NULL) throw();
	virtual ~XmlTok() throw();
	bool findChild(string const& name) throw();
	XmlTok* getNextChild() throw();
	XmlTok* getParent() throw();
	string const& getAttr(string const& name) const throw();
	// NOTE: see setData
	string toString(int indent = 0) const throw();
	// NOTE: see setData
	const string& getData() const throw();
	/*
	 * Setters
	 */
	XmlTok* addChild(string const& name) throw();
	void setAttr(string const& name, string const& attr) throw();
	// NOTE: DATA is not get'able if you have children as well!
	void setData(string const& data) throw();
	/*
	 * Load/Save
	 */
	bool load(string const& filename) throw();
	bool save(string const& filename) const throw();
	/*
	 * Iterators
	 */
	typedef vector<XmlTok*>::iterator iterator;
	iterator begin() { return children.begin(); };
	iterator end() { return children.end(); };

	void clear() throw();
private:
	string data;
	XmlTok* parent;
	string name;
	typedef vector<XmlTok*> Children;
	Children children;
	Children::iterator found;
	StringMap attributes;
};

} //namespace qhub

#endif //_INCLUDED_XMLTOK_H_
