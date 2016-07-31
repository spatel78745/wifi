#ifndef WPASUPPLICANT_H
#define WPA_SUPPLICANT_H

#include <vector>
#include <thread>

#include "WifiManagerException.h"

using namespace std;

struct ScanResult
{
	string bssid;
	int frequency;
	int signal_level;
	string flags;
	string ssid;
};

struct Network
{
	int network_id;
	string ssid;
	string bssid;
	string flags;
};

class WpaSupplicant
{
	public:

		static WpaSupplicant& getInstance();

		void scan(void);

		void get_scan_results();

		const vector<ScanResult>& scan_results();

		int add_network();

		void remove_network(int network_id);

		void remove_all_networks();

		void set_network(int network_id, const string variable, const string value);

		void disable_network(int network_id);

		void enable_network(int network_id);

		void connect(const string ssid, const string psk="");

		void disconnect();

		vector<Network> list_networks();

		const string& ifname();

		Network get_current_network();

		void wpa_cli(string cmd, string& output);

		void wpa_cli(string cmd);

	private:
		vector<ScanResult> m_scan_results;

		bool failed(string &output);

		WpaSupplicant();

		WpaSupplicant(const WpaSupplicant& rs);

		WpaSupplicant& operator=(const WpaSupplicant& rs);

		~WpaSupplicant() { };

		static WpaSupplicant *m_instance;
};

#endif
