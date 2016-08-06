#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>

using namespace std;

#define check(A, M, ...)                                      \
	if((A)) {                                                   \
		char str[256];                                            \
                                                              \
		sprintf(str, M ,  ## __VA_ARGS__);                        \
		throw WifiManagerException(__FILE__, __LINE__, str);      \
	}

#define debug(M, ...) fprintf(stderr, "DEBUG %s:%s:%d: " M "\n", __FILE__, \
		__func__, __LINE__, ##__VA_ARGS__)

void shell(const string command, string& output);

vector<string> split(string str, char delim);

#endif

