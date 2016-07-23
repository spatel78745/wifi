#ifndef WIFIMANAGEREXCEPTION_H
#define WIFIMANAGEREXCEPTION_H

#include <string>
#include <sstream>

using namespace std;

class WifiManagerException: public exception
{
	public:
		WifiManagerException(const char *file, const int line, const char *what)
		{
			stringstream ss;

			ss << "[" << file << ":" << line << "] " << what;

			m_what = ss.str();
		}

		WifiManagerException(const char *file, const int line, string what)
		{
			stringstream ss;

			ss << "[" << file << ":" << line << "] " << what;

			m_what = ss.str();
		}

	private:
		string m_what;

		virtual const char *what() const throw()
		{
			return m_what.c_str();
		}
};

#endif

