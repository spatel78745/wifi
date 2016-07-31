#include <iostream>
#include <thread>
#include <chrono>

#include "util.h"
#include "WpaSupplicant.h"

using namespace std;
using namespace std::chrono;

static WpaSupplicant& supp = WpaSupplicant::getInstance();

const char *test_ssid = "Mine not yours";
const char *test_psk = "bapa1602";

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

void test_scan()
{
	supp.get_scan_results();

	auto& scan_results = supp.scan_results();

	for(auto& sr: scan_results)
	{
		debug("scan_result: %s %d %d %s %s", sr.bssid.c_str(), sr.frequency, sr.signal_level,
				sr.flags.c_str(), sr.ssid.c_str());
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

void test_wpa_cli()
{
	string cmd;
	string out;

	while(cin && (cmd != "quit"))
	{
		try
		{
			cout << "Enter a wpa_supplicant command" << endl;
			getline(cin, cmd);
			if (cin && cmd != "quit")
			{
				supp.wpa_cli(cmd, out);
				cout << "======================" << endl;
				cout << out << endl;
				cout << "++++++++++++++++++++++" << endl;
			}
		}
		catch(exception& e)
		{
			cout << "Exception: " << e.what() << endl;
		}
	}
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

void connect_to_test_network()
{
  cout << "Connecting to test network" << endl;

	try
	{
		supp.connect(test_ssid, test_psk);
	}
	catch(exception& e)
	{
		cerr << "Failed to connect: " << e.what() << endl;
	}
}

void test_connect()
{

	cout << "test_connect" << endl;
	try
	{
		supp.connect("TEST_CONNECT");
	}
	catch(exception& e)
	{
		cerr << "Failed to connect: " << e.what() << endl;
	}
}
#if 0
void test_connect_disconnect()
{
	cout << "test_connect_disconnect()" << endl;

	using namespace chrono;

	try
	{
		cout << "connecting..." << endl;
		supp.connect("TEST_CONNECT_DISCONNECT");
		cout << "waiting for 60s" << endl;
		this_thread::sleep_for(seconds(60));
		cout << "disconnecting..." << endl;
		supp.disconnect();
	}
	catch(exception& e)
	{
		cerr << "Failed" << endl;
	}
}
#endif
void test_add_network()
{
	cout << "Network id: " << supp.add_network() << endl;
	cout << "Network id: " << supp.add_network() << endl;
	cout << "Network id: " << supp.add_network() << endl;
}

void test_set_network()
{
	int network_id = supp.add_network();
	cout << "set_network " << network_id << " ssid TEST_NET" << endl;
	supp.set_network(network_id, "ssid", "TEST_NET");
	cout << "======================" << endl;
	cout << "Results:" << endl;

	string out;
	supp.wpa_cli("list_networks", out);
	cout << out << endl;
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

int main(int argc, char *argv[])
{
	debug("supp=%p", &supp);
	this_thread::sleep_for(seconds(3600));
}
