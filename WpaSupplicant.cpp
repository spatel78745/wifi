#define NDEBUG
#include <sstream>
#include <iostream>
#include <cassert>
#include <thread>
#include <map>
#include <fstream>
#include <atomic>
#include "util.h"
#include "WpaSupplicant.h"

WpaSupplicant * WpaSupplicant::m_instance = nullptr;

#define WPA_SUPPLICANT_SERVICE			"wpa_supplicant-rmf.service"
#define HOSTAPD_SERVICE							"hostapd-rmf.service"
#define HOSTAPD_CONFIG_FILE					"/tmp/hostapd.conf"
#define WPA_SUPPLICANT_CONFIG_FILE	"/tmp/wpa_supplicant.conf"
#define SCAN_DELAY									60
#define UPDATE_STATUS_DELAY					60

static bool switch_services = true;

using namespace std::chrono;

void periodic_task(void (*task)());

struct
{
	typedef void (&task_func_t)();

	typedef struct
	{
		int delay;
		bool enabled;
		const char *name;
		bool  debug;
	} task_data_t;

	map<void (*)(), task_data_t> m_tasks;
	mutex m_mutex;

	void add_task(task_func_t task, int delay, const char *name)
	{
		m_tasks[task] = task_data_t{ delay, true, name, true };

		cout << "Starting task " << name << endl;
		thread(periodic_task, &task).detach();
	}

	void set_enabled(task_func_t task, bool enabled)
	{
		// Kick the task since it's thread might be sleeping
		task();

		unique_lock<mutex> lck{m_mutex};

		m_tasks[task].enabled = enabled;
	}

	bool get_enabled(task_func_t task, bool enabled)
	{
		unique_lock<mutex> lck{m_mutex};

		return m_tasks[task].enabled;
	}

	void set_debug(task_func_t task, bool debug)
	{
		m_tasks[task].debug = debug;
	}
} task_manager;

void periodic_task(void (*task)())
{
	string name = task_manager.m_tasks[task].name;
	int delay = task_manager.m_tasks[task].delay;

	while(true)
	{
		if (task_manager.m_tasks[task].enabled) task();
		this_thread::sleep_for(seconds(delay));
	}
}

bool failed(string& output)
{
	if ( output.find("FAIL") != string::npos ) return true;

	return false;
}

static void wpa_cli(string cmd, string& output)
{
	shell("wpa_cli " + string(cmd), output);

	if (failed(output))
	{
		throw WifiManagerException(__FILE__, __LINE__, cmd + " failed (" + output + ")");
	}
}

static void wpa_cli(string cmd)
{
	string output;

	wpa_cli(cmd, output);
}

static void scan_task()
{
	try
	{
		wpa_cli("scan");
	}
	catch(exception& e)
	{
		debug("Scan failed: %s", e.what());
	}
}

static void update_status_task()
{
	try
	{
		WpaSupplicant::getInstance().update_status();
	}
	catch(exception& e)
	{
		debug("Update status failed: %s", e.what());
	}
}

static int run(string cmd)
{
	debug("Running %s", cmd.c_str());
	int ret = system(cmd.c_str());

	if (ret == -1)
	{
		throw WifiManagerException(__FILE__, __LINE__, "system(" + string(cmd) + ") failed");
	}

	return WEXITSTATUS(ret);
}

void write_config(const map<string, string>& config, const char *filename)
{
	ofstream config_file(filename);

	debug("Writing config file %s", filename);
	for(auto& c: config)
	{
		config_file << c.first << "=" << c.second << endl;
		cout << c.first << "=" << c.second << endl;
	}
}

void WpaSupplicant::stop_ap()
{
	debug("Stopping " HOSTAPD_SERVICE " service");
	if (run("systemctl stop " + string(HOSTAPD_SERVICE)))
	{
		throw WifiManagerException(__FILE__, __LINE__, HOSTAPD_SERVICE " failed to stop");
	}
}

void WpaSupplicant::stop_sta()
{
	debug("Stopping tasks");
	stop_scan_task();
	stop_update_status_task();

	debug("Stopping " WPA_SUPPLICANT_SERVICE " service");
	if (run("systemctl stop " + string(WPA_SUPPLICANT_SERVICE)))
	{
		throw WifiManagerException(__FILE__, __LINE__, WPA_SUPPLICANT_SERVICE " failed to stop");
	}
}

static void switch_service(string old_service, string new_service)
{
	if (!switch_services) return;

	debug("Stopping %s", old_service.c_str());
	run("systemctl stop " + old_service);
	debug("Stopping %s", new_service.c_str());
	run("systemctl stop " + new_service);
	debug("Starting %s", new_service.c_str());
	run("systemctl start " + new_service);

	debug("Checking if %s is active", new_service.c_str());
	if (run("systemctl is-active " + new_service))
	{
		throw WifiManagerException(__FILE__, __LINE__, new_service + " failed to start");
	}
}

WpaSupplicant::WpaSupplicant()
{
}

WpaSupplicant::WpaSupplicant(const WpaSupplicant& rs)
{
	m_instance = rs.m_instance;
}

WpaSupplicant& WpaSupplicant::operator=(const WpaSupplicant& rs)
{
	if (this != &rs) m_instance = rs.m_instance;

	return *this;
}

WpaSupplicant& WpaSupplicant::getInstance()
{
	static WpaSupplicant theInstance;

	m_instance = &theInstance;

	return *m_instance;
}


void WpaSupplicant::scan(void) { wpa_cli("scan"); }

void WpaSupplicant::set_network(int network_id, const string variable, const string value)
{
	assert(network_id >= 0);
	wpa_cli("set_network " + to_string(network_id) + " " + variable + " " + value);
}

void WpaSupplicant::sta(map<string, string>& config)
{
	write_config(config, WPA_SUPPLICANT_CONFIG_FILE);

	debug("Starting " WPA_SUPPLICANT_SERVICE);
	switch_service(HOSTAPD_SERVICE, WPA_SUPPLICANT_SERVICE);

	debug("Enabling scan_task");
	task_manager.set_enabled(scan_task, true);
	debug("Enabling update_status_task");
	task_manager.set_enabled(update_status_task, true);

	remove_all_networks();

	if (config["ssid"].empty())
	{
		debug("No SSID. Do nothing");
		return;
	}

	int network_id = add_network();
	set_network(network_id, "ssid", "\\\"" + config["ssid"] + "\\\"");
	set_network(network_id, "psk", "\\\"" + config["psk"] + "\\\"");

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

vector<ScanResult> WpaSupplicant::scan_results()
{
	string str;

	wpa_cli("scan_results", str);

	// Discard selected interface and column headings
	istringstream ss(str);
	getline(ss, str);
	getline(ss, str);

	ScanResult result;
	vector<ScanResult> all_results;

	while(getline(ss, str))
	{
		vector<string> fields = split(str, '\t');

		result.bssid        = fields[0];
		result.frequency    = stoi(fields[1]);
		result.signal_level = stoi(fields[2]);
		result.flags        = fields[3];
		result.ssid         = fields[4];

		all_results.push_back(result);
	}

	debug("%d results", (int)all_results.size());

	return all_results;
}

void WpaSupplicant::update_status()
{
	string str;

	wpa_cli("status", str);

	unique_lock<mutex> lck { m_status_mutex };

	m_status["bssid"]               = "";
	m_status["frequency"]           = "";
	m_status["ssid"]                = "";
	m_status["id"]                  = "";
	m_status["mode"]                = "";
	m_status["pairwise_cipher"]     = "";
	m_status["group_cipher"]        = "";
	m_status["key_mgmt"]            = "";
	m_status["wpa_state"]           = "";
	m_status["ip_address"]          = "";
	m_status["p2p_device_address"]  = "";
	m_status["address"]             = "";
	m_status["uuid"]                = "";

	istringstream ss(str);
	// Discard selected interface
	getline(ss, str);

	while(getline(ss, str))
	{
		vector<string> fields = split(str, '=');

		m_status[fields[0]] = fields[1];
		debug("set %s to %s", fields[0].c_str(), fields[1].c_str());
	}
}

map<string, string> WpaSupplicant::status()
{
	unique_lock<mutex> lck { m_status_mutex };
	return m_status;
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

void WpaSupplicant::ap(map<string, string>& config)
{
	debug("Stopping scan_task");
	task_manager.set_enabled(scan_task, false);
	debug("Stopping update_status_task");
	task_manager.set_enabled(update_status_task, false);

	write_config(config, HOSTAPD_CONFIG_FILE);

	debug("Restarting HOSTAPD");
	switch_service(WPA_SUPPLICANT_SERVICE, HOSTAPD_SERVICE);
}

void WpaSupplicant::stop_scan_task()
{
	task_manager.set_enabled(scan_task, false);
}

void WpaSupplicant::stop_update_status_task()
{
	task_manager.set_enabled(update_status_task, false);
}

void WpaSupplicant::rfkill()
{
	//TODO: actually call rfkill
	stop_ap();
	stop_sta();
}
