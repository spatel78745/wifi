#ifndef WPASUPPLICANT_H
#define WPA_SUPPLICANT_H

#include <vector>
#include <thread>
#include <map>
#include <mutex>

#include "WifiManagerException.h"

using namespace std;

struct ScanResult
{
	string bssid;
	int frequency;
	int signal_level;
	string flags;
	string ssid;
	~ScanResult() {} // Required because of -Werror=inline
};

struct Network
{
	int network_id;
	string ssid;
	string bssid;
	string flags;
	~Network() {} // Required because of -Werror=inline
};

class WpaSupplicant
{
	public:

		static WpaSupplicant& getInstance();

		void scan(void);

		vector<ScanResult> scan_results();

		int add_network();

		void remove_network(int network_id);

		void remove_all_networks();

		void set_network(int network_id, const string variable, const string value);

		void disable_network(int network_id);

		void enable_network(int network_id);

		void sta(map<string, string>& config);

		void disconnect();

		vector<Network> list_networks();

		const string& ifname();

		Network get_current_network();

		void update_status();

		map<string, string> status();

		void ap(map<string, string>& config);

		void stop_ap();

		void stop_sta();

		map<string, string> ap_config();

		void stop_scan_task();

		void stop_update_status_task();

		void rfkill();

	private:
		bool failed(string &output);

		WpaSupplicant();

		WpaSupplicant(const WpaSupplicant& rs);

		WpaSupplicant& operator=(const WpaSupplicant& rs);

		~WpaSupplicant() { };

		static WpaSupplicant *m_instance;

		mutex m_status_mutex;

		map<string, string> m_status;
};

#endif
