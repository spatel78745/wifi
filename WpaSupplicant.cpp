#define NDEBUG
#include <sstream>
#include <iostream>
#include <cassert>
#include "util.h"
#include "WpaSupplicant.h"

WpaSupplicant::WpaSupplicant()
{
}

bool WpaSupplicant::failed(string& output)
{
	if ( output.find("FAIL") != string::npos ) return true;

	return false;
}

void WpaSupplicant::wpa_cli(string cmd, string& output)
{
	shell("wpa_cli " + string(cmd), output);

	if (failed(output))
	{
		throw WifiManagerException(__FILE__, __LINE__, cmd + " failed (" + output + ")");
	}
}

void WpaSupplicant::wpa_cli(string cmd)
{
	string output;

	wpa_cli(cmd, output);
}

void WpaSupplicant::scan(void) { wpa_cli("scan"); }

void WpaSupplicant::set_network(int network_id, const string variable, const string value)
{
	assert(network_id >= 0);
	wpa_cli("set_network " + to_string(network_id) + " " + variable + " " + value);
}

void WpaSupplicant::connect(const string ssid, const string psk)
{
	remove_all_networks();

	int network_id = add_network();
	set_network(network_id, "ssid", "\\\"" + ssid + "\\\"");

	if (psk.empty()) set_network(network_id, "key_mgmt", "NONE");
	else             set_network(network_id, "psk", "\\\"" + psk + "\\\"");

	enable_network(network_id);
}

vector<Network> WpaSupplicant::list_networks()
{
	string str;

	wpa_cli("list_networks", str);

	// Discard column headings
	istringstream ss(str);
	getline(ss, str);
	getline(ss, str);

	Network nw;
	vector<Network> nw_list;

  while(getline(ss, str))
  {
    vector<string> fields = split(str, '\t');

    nw.network_id = stoi(fields[0]);
    nw.ssid       = fields[1];
    nw.bssid      = fields[2];
    nw.flags      = fields[3];

    nw_list.push_back(nw);
  }

  return nw_list;
}

Network WpaSupplicant::get_current_network()
{
	vector<Network> nw_list = list_networks();

	for(auto& nw: nw_list)
	{
		if (nw.flags.find("CURRENT") != string::npos)
		{
			return nw;
		}
	}

	return (Network{-1});
}

void WpaSupplicant::get_scan_results()
{
	string str;

	wpa_cli("scan_results", str);

	// Discard selected interface and column headings
	istringstream ss(str);
	getline(ss, str);
	getline(ss, str);

	ScanResult sr;
	m_scan_results.clear();
	while(ss >> sr.bssid >> sr.frequency >> sr.signal_level >> sr.flags >> sr.ssid)
	{
		m_scan_results.push_back(sr);
	}

	debug("%lu results", m_scan_results.size());
}

int WpaSupplicant::add_network()
{
	string output;

	wpa_cli("add_network", output);
	output.erase(0, output.find_first_of("\n"));

	return stoi(output);
}

void WpaSupplicant::enable_network(int network_id)
{
	wpa_cli("enable_network " + to_string(network_id));
}

void WpaSupplicant::disable_network(int network_id)
{
	wpa_cli("disable_network" + to_string(network_id));
}

void WpaSupplicant::remove_network(int network_id)
{
	wpa_cli("remove_network " + to_string(network_id));
}

void WpaSupplicant::remove_all_networks()
{
	vector<Network> nw_list = list_networks();

	for(auto& nw: nw_list) remove_network(nw.network_id);
}

const vector<ScanResult>& WpaSupplicant::scan_results()
{
	return m_scan_results;
}
