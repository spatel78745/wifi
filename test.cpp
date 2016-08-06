#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

#include "util.h"
#include "WpaSupplicant.h"

using namespace std;
using namespace std::chrono;

static WpaSupplicant& supp = WpaSupplicant::getInstance();

const char *test_ssid = "Mine not yours";
const char *test_psk = "bapa1602";

const char *confd_lasterr()
{
	static const char *str = "Fake error";

	return str;
}

void test_macro()
{
	int a = 10;

	check(a != 10, "maapi_create failed %d", 999);
}

void test_stoi()
{
	string str;
	int		 n;

	while(true)
	{
		cout << "Enter a number: " << endl;
		cin >> str;
		n = stoi(str);
		cout << "The int is: " << n << endl;
	}
}

void test_scan_results()
{
	int iteration = 0;

	while(true)
	{
		const vector<ScanResult>& scan_results = supp.scan_results();

		for(auto& sr: scan_results)
		{
			debug("scan_result: (%d) %s %d %d %s %s", iteration, sr.bssid.c_str(), sr.frequency, sr.signal_level,
					sr.flags.c_str(), sr.ssid.c_str());
		}
		++iteration;
		this_thread::sleep_for(seconds(5));
	}
}

void test_remove_network()
{
	int n1 = supp.add_network();
	int n2 = supp.add_network();
	int n3 = supp.add_network();

	cout << "Added networks " << n1 << " " << n2 << " " << n3 << endl;
	cout << "Removing network " << n1 << endl;
	supp.remove_network(n1);
	cout << "Removing network " << n2 << endl;
	supp.remove_network(n2);
	cout << "Removing network " << n3 << endl;
	supp.remove_network(n3);
}

void test_manual_connect()
{
	try
	{
		int network_id = supp.add_network();
		supp.set_network(network_id, "ssid", "\\\"TEST_NETWORK1\\\"");
		supp.set_network(network_id, "key_mgmt", "NONE");
		supp.enable_network(network_id);
	}
	catch(exception& e)
	{
		cerr << "Failed to connect: " << e.what() << endl;
	}
}

void test_add_network()
{
	cout << "Network id: " << supp.add_network() << endl;
	cout << "Network id: " << supp.add_network() << endl;
	cout << "Network id: " << supp.add_network() << endl;
}

void add_some_networks()
{
	int nid;

	nid = supp.add_network();
	supp.set_network(nid, "ssid", "\\\"SSID" + to_string(nid) + "\\\"");

	nid = supp.add_network();
	supp.set_network(nid, "ssid", "\\\"SSID" + to_string(nid) + "\\\"");

	nid = supp.add_network();
	supp.set_network(nid, "ssid", "\\\"SSID" + to_string(nid) + "\\\"");
}

void print_network(Network nw)
{
		cout << "network_id: " 	<< nw.network_id << " "
				 << "ssid: " 			 	<< nw.ssid       << " "
				 << "bssid: "				<< nw.bssid      << " "
				 << "flags: "				<< nw.flags      << " "
				 << endl;
}

void test_list_networks()
{
	add_some_networks();

	vector<Network> nw_list = supp.list_networks();
	for(auto& nw: nw_list)
	{
		print_network(nw);
	}
}

void test_remove_all_networks()
{

	add_some_networks();
	supp.remove_all_networks();
}

void print_string_vector(vector<string> vec)
{
  for(auto& iter: vec)
  {
    cout << iter << endl;
  }
}

void split_report(string s1, vector<string> fields)
{
  cout << s1 << " -- num fields " << fields.size() << endl;
  print_string_vector(fields);
  cout << "+++++++++" << endl;
}

void test_split()
{
  string s1;
  vector<string> fields;

  s1 = "this\tis\ta\tstring";
  fields = split(s1, '\t');
  split_report(s1, fields);

  s1 = "";
  fields = split(s1, '\t');
  split_report(s1, fields);

  s1 = "\t";
  fields = split(s1, '\t');
  split_report(s1, fields);

  s1 = "two fields\t";
  fields = split(s1, '\t');
  split_report(s1, fields);

  s1 = "no tabs";
  fields = split(s1, '\t');
  split_report(s1, fields);
}

void test_status()
{
	int iteration = 0;

	while(true)
	{
		map<string, string> status = supp.status();
		for(auto& s: status)
		{
			cout << iteration << ": " << s.first << "=" << s.second << endl;
		}
		this_thread::sleep_for(seconds(5));
	}
}

void test_start_ap()
{
	map<string, string> config = {
		{ "ctrl_interface", "/run/hostapd" },
		{ "interface", "wlan0" },
		{ "driver", "nl80211" },
		{ "ssid", "SAMEER" },
		{ "logger_syslog", "-1"},
		{ "logger_syslog_level", "1" }
	};

	supp.ap(config);
}

void test_cli()
{
	map<string, string> ap_config = {
		{ "ctrl_interface" , "/run/hostapd" } ,
		{ "interface"      , "wlan0"        } ,
		{ "driver"         , "nl80211"      } ,
		{ "ssid"           , "SAMEER"       } ,
		{ "channel"        , "1"            }
	};

	map<string, string> sta_config = {
		{ "ctrl_interface" , "/run/wpa_supplicant" } ,
		{ "update_config"  , "1"                   } ,
	};

	string cmd;

	cout << "Stopping ap and sta" << endl;
	supp.stop_ap();
	supp.stop_sta();

	cout << ">> " << flush;
	while(cin)
	{
		cin >> cmd;
		if (cmd == "sc")
		{
			string type, key, val;

			cin >> type >> key >> val;
			cout << "Set config " << type << " " << key << "=" << val << endl;
			if   	  (type == "ap") 	ap_config[key] = val;
			else if (type == "sta") sta_config[key] = val;
			else 										cout << "Error: unknown config" << endl;
		}
		else if (cmd == "pc")
		{
			string type;
			map<string, string> config;

			cin >> type;
			if      (type == "ap") 	config = ap_config;
			else if (type == "sta") config = sta_config;
			else 										cout << "Error: unknown config" << endl;

			cout << "Config" << endl;
			for(auto& p: config)
			{
				cout << p.first << "=" << p.second << endl;
			}
		}
		else if (cmd == "ap")
		{
			supp.ap(ap_config);
			cout << "AP started" << endl;
		}
		else if (cmd == "stop-ap")
		{
			supp.stop_ap();
			cout << "AP stopped" << endl;
			system("systemctl is-active hostapd-rmf.service");
		}
		else if (cmd == "sta")
		{
			supp.sta(sta_config);
		}
		else if (cmd == "stop-sta")
		{
			supp.stop_sta();
			cout << "STA stopped" << endl;
			system("systemctl is-active wpa_supplicant-rmf.service");
		}
		else if (cmd == "rfkill")
		{
			supp.stop_sta();
			cout << "STA stopped" << endl;
			supp.stop_ap();
			cout << "AP stopped" << endl;
			cout << "STA status" << endl;
			system("systemctl is-active wpa_supplicant-rmf.service");
			cout << "AP status" << endl;
			system("systemctl is-active hostapd-rmf.service");
		}
		else if (cmd == "stat")
		{
			cout << "wpa_supplicant" << endl;
			system("systemctl is-active wpa_supplicant-rmf.service");
			cout << "hostapd" << endl;
			system("systemctl is-active hostapd-rmf.service");
		}
		else if (cmd == "scr")
		{
			vector<ScanResult> sr = supp.scan_results();

			struct comp {
				bool operator() (ScanResult a, ScanResult b)
				{
					return a.signal_level < b.signal_level;
				}
			} comp;

			sort(sr.begin(), sr.end(), comp);
			map<string, int> ssids;

			for(auto& r: sr)
			{
				ssids[r.ssid] = r.signal_level;
			}

			for(auto& ap: ssids)
			{
				cout << ap.first << " " << ap.second << endl;
			}

			cout << "DONE" << endl;
		}
		else if (cmd == "q")
		{
			exit(0);
		}
		else
		{
			cout << "Unknown command: " << cmd << endl;
		}

		cout << ">> " << flush;
	}
}

int main(int argc, char *argv[])
{
	test_macro();
//	test_cli();
}
