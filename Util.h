// vim:ts=4:sw=4:noet
#ifndef _INCLUDED_UTIL_H_
#define _INCLUDED_UTIL_H_

#include <string>
#include <vector>
#include <stdlib.h>
#include "string8.h"

namespace qhub {

using namespace std;

typedef vector<string> StringList;
typedef void* voidPtr;


class Util {
public:
	static string const emptyString;
	static int const emptyInt;
	static voidPtr const emptyVoidPtr;
	static StringList const emptyStringList;

	static void log(string const& message, int level=0) throw();

	static string format(const char* msg, ...);
		
	static string errnoToString(int err) throw()
	{
		return strerror(err);
	};

	static int toInt(char const* p) throw() {
		return atoi(p);
	};
	static int toInt(string const& s) throw() {
		return toInt(s.c_str());
	};

	static string toString(void* p) throw() {
		char buf[32];
		snprintf(buf, 32, "%p", p);
		return buf;
	};
	static string toString(int i) throw() {
		char buf[32];
		snprintf(buf, 32, "%i", i);
		return buf;
	};

	static string8 genRand192() throw() {
		u_int8_t buf[24];
		for(unsigned i = 0; i < 24; i += sizeof(int)) {
			int r = rand();
			memcpy(buf + i, &r, sizeof(int) <= 24 - i ? sizeof(int) : 24 - i);
		}
		return string8(buf, 24);
	};

	static StringList stringTokenize(string const& msg, char token = ' ') throw();
	static StringList lazyStringTokenize(string const& msg, char token = ' ') throw();
	static StringList lazyQuotedStringTokenize(string const& msg) throw(); // token = ' ', quote = '"'

};

} //namespace qhub

#endif //_INCLUDED_UTIL_H_
