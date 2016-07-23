#include "WifiManagerException.h"
#include "util.h"
#include <string>
#include <vector>

using namespace std;

void shell(string command, string& output)
{
	FILE *fp;

	command += " 2>&1";

	fp = popen(command.c_str(), "r");

	if (fp == NULL)
	{
		throw WifiManagerException(__FILE__, __LINE__, "popen() failed command (" + string(command) + ")");
	}

	char c;
	while(!feof(fp))
	{
		c = fgetc(fp);
		if (!feof(fp)) output.push_back(c);
	}

	int rc = pclose(fp);
	if (rc == -1)
	{
		throw WifiManagerException(__FILE__, __LINE__, "pclose() failed " + string(command) + ")");
	}

	if (WEXITSTATUS(rc) != 0)
	{
		throw WifiManagerException(__FILE__, __LINE__, "command failed (" + string(command) + ")");
	}
}

vector<string> split(string str, char delim)
{
  vector<string> fields;

  string::size_type start, end;

  for(start = end = 0; end != str.size(); ++end)
  {
    if (str[end] == delim)
    {
      fields.push_back(str.substr(start, end - start));
      start = end + 1;
    }
  }

  fields.push_back(str.substr(start, end - start));

  return fields;
}
