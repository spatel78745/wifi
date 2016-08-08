#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <iostream>
#include <confd_lib.h>
#include <confd_cdb.h>
#include <confd_dp.h>

#include "confd_maapi.h"
#include "util.h"
#include "wifi.h"

#include "WpaSupplicant.h"

using namespace std;

///////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////
#define rmf__wifiMode_off						wifi_mode_off
#define rmf__wifiMode_station				wifi_mode_station
#define rmf__wifiMode_access_point	wifi_mode_access_point
#define rmf__ns											wifi__ns
#define rmf__ipIfaceType_dynamic		wifi_dynamic
#define rmf__ipIfaceType_static			wifi_static
#define rmf_ifs_bssid								wifi_bssid
#define rmf_ifs_frequency						wifi_frequency
#define rmf_ifs_signal_level				wifi_signal_level
#define rmf_ifs_flags								wifi_flags
#define rmf_ifs_ssid								wifi_ssid

#define WIFI_DIR							"/wifi"
#define BUF_SIZE							256

///////////////////////////////////////////////////////////////////////////////
// Data
///////////////////////////////////////////////////////////////////////////////
struct wifi_config
{
	int mode = rmf__wifiMode_off;

	struct
	{
		string ssid;
		string psk;
		int ip_address_src;
		string key_mgmt = "NONE";
	} station;

	struct
	{
		string ssid;
		string psk;
	} access_point;
};
//
// Base hostapd configuration
map<string, string> ap_config = {
		{ "ctrl_interface" , "/run/hostapd" } ,
		{ "interface"      , "wlan0"        } ,
		{ "driver"         , "nl80211"      } ,
		{ "ssid"           , "SAMEER"       } ,
		{ "channel"        , "1"            }
};

// Base wpa_supplicant configuration
map<string, string> sta_config = {
		{ "ctrl_interface" , "/run/wpa_supplicant" } ,
		{ "update_config"  , "1"                   } ,
};

static WpaSupplicant& supp = WpaSupplicant::getInstance();

static int ctlsock;
static int workersock;
static struct confd_daemon_ctx *dctx;

bool operator==(const struct wifi_config& lhs, const struct wifi_config& rhs)
{
	return (   lhs.mode              == rhs.mode
					&& lhs.station.ssid      == rhs.station.ssid
					&& lhs.station.psk       == rhs.station.psk
					&& lhs.access_point.ssid == rhs.access_point.ssid
					&& lhs.access_point.psk  == rhs.access_point.psk
				 );
}

bool operator!=(const struct wifi_config& lhs, const struct wifi_config& rhs) { return !(lhs == rhs); }

static void print_config(const char *title, const struct wifi_config& config)
{
	// Convert mode to a string
	string mode_str;
	int mode = config.mode;

	if      (mode == rmf__wifiMode_off)					 mode_str = "off";
	else if (mode == rmf__wifiMode_station)			 mode_str = "station";
	else if (mode == rmf__wifiMode_access_point) mode_str = "ap";

	cout << "BEGIN CONFIG: " << title << endl;
	cout << "------------" << endl;
	cout << "mode: " << mode_str << endl;
	cout << "station.ssid: " << config.station.ssid << endl;
	cout << "station.psk: " << config.station.psk << endl;
	cout << "station.ip_address_src: " << config.station.ip_address_src << endl;
	cout << "access_point.ssid: " << config.access_point.ssid << endl;
	cout << "access_point.psk: " << config.access_point.psk << endl;
	cout << "END CONFIG" << endl;
	cout << "----------" << endl;
}

///////////////////////////////////////////////////////////////////////////////
// Local functions
///////////////////////////////////////////////////////////////////////////////
static bool path_exists(int msock, int th, const char *path)
{
	int ret = maapi_exists(msock, th, path);
	if (confd_errno != CONFD_OK)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_exists ") + path);
	}

	return ret;
}

void write_leaf(int msock, int th, confd_value_t *v, const char *path, ...)
{
	int ret;

	if (!path_exists(msock, th, path)) ok(maapi_create(msock, th, path), "maapi_create");

	va_list arguments;
  va_start(arguments, path);

	ok(maapi_vset_elem(msock, th, &v, path, arguments), "maapi_vset_elem");
}

void start_transaction(int& sock, int& th)
{
	check(((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0), "socket failed");

	struct sockaddr_in addr;

	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFD_PORT);

	debug("maapi_connect");
	if (maapi_connect(sock, (struct sockaddr*)&addr,
				sizeof (struct sockaddr_in)) < 0)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_connect"));
	}

	struct confd_ip ip;
	const char *groups[] = { "admin" };

	ip.af = AF_INET;
	inet_pton(AF_INET, "127.0.0.1", &ip.ip.v4);

	debug("maapi_start_user_session as admin");
	if (maapi_start_user_session(sock, "admin", "system", groups, 1, &ip,
				CONFD_PROTO_TCP) != CONFD_OK)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_start_user_session"));
	}

	debug("maapi_start_trans");
	if ((th = maapi_start_trans(sock, CONFD_RUNNING, CONFD_READ_WRITE)) < 0)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_connect"));
	}
}

void finish_transaction(int msock, int th)
{
	ok(maapi_apply_trans(msock, th, 0), "maapi_apply_trans");
	ok(maapi_finish_trans(msock, th), "maapi_finish_trans");
	ok(maapi_end_user_session(msock), "maapi_end_user_session");

	(void)maapi_close(msock);
}

string get_string_leaf(int cdbsock, const char *path)
{
	char buf[BUF_SIZE];

	get_leaf(cdb_get_str(cdbsock, buf, BUF_SIZE, path), "cdb_get_str");
	return string(buf);
}

///////////////////////////////////////////////////////////////////////////////
// Subscription handlers
///////////////////////////////////////////////////////////////////////////////
static void read_config(int cdbsock, struct wifi_config& config)
{
	char buf[BUF_SIZE];

	ok(cdb_set_namespace(cdbsock, rmf__ns), "cdb_set_namespace");
	ok(cdb_cd(cdbsock, "%s[0]", WIFI_DIR), "cdb_cd");

	// Read STA config
	get_leaf(cdb_get_enum_value(cdbsock, &config.mode, "mode"), "cdb_get_enum_value");
	get_leaf(cdb_get_enum_value(cdbsock, &config.station.ip_address_src, "station/ip-address-src"), "cdb_get_enum_value");
	config.station.ssid = get_string_leaf(cdbsock, "station/ssid");
	config.station.psk = get_string_leaf(cdbsock, "station/psk");

	// Read AP config
	config.access_point.ssid = get_string_leaf(cdbsock, "access_point/ssid");
	config.access_point.psk = get_string_leaf(cdbsock, "access_point/psk");

	print_config("read_config", config);
}

static void apply_config(struct wifi_config& config)
{
	print_config("apply_config", config);

	switch(config.mode)
	{
		case rmf__wifiMode_off:
			supp.rfkill();
			break;
		case rmf__wifiMode_station:
			sta_config["psk"      ] = config.station.psk;
			sta_config["ssid"     ] = config.station.ssid;
			sta_config["key_mgmt" ] = config.station.key_mgmt;
			supp.sta(sta_config);
			break;
		case rmf__wifiMode_access_point:
			supp.start_ap(hostapd_config);
			break;
		default:
			debug("Error: unknown wifi mode");
			break
		}
	}
}

void init_daemon()
{
	struct confd_action_cbs acb;
	struct sockaddr_in addr;

	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFD_PORT);

	if (confd_load_schemas((struct sockaddr*)&addr,
												 sizeof (struct sockaddr_in)) != CONFD_OK)
			confd_fatal("Failed to load schemas from confd\n");
	if ((dctx = confd_init_daemon("actions_daemon")) == NULL)
			confd_fatal("Failed to initialize ConfD\n");

	if ((ctlsock = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
			confd_fatal("Failed to open ctlsocket\n");

	/* Create the first control socket, all requests to */
	/* create new transactions arrive here */

	if (confd_connect(dctx, ctlsock, CONTROL_SOCKET, (struct sockaddr*)&addr,
										sizeof (struct sockaddr_in)) < 0)
			confd_fatal("Failed to confd_connect() to confd \n");

	debug("Control socket %d: ", ctlsock);


	/* Also establish a workersocket, this is the most simple */
	/* case where we have just one ctlsock and one workersock */

	if ((workersock = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
			confd_fatal("Failed to open workersocket\n");
	if (confd_connect(dctx, workersock, WORKER_SOCKET,(struct sockaddr*)&addr,
										sizeof (struct sockaddr_in)) < 0)
			confd_fatal("Failed to confd_connect() to confd \n");

	debug("Worker socket: %d", workersock);
}

void make_wifi_node()
{
	int sock, th;

	start_transaction(sock, th);

	// Create wifi{0}
	int ret = maapi_create(sock, th, "/wifi{1}");
	if (ret != CONFD_OK && confd_errno != CONFD_ERR_ALREADY_EXISTS)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_create"));
	}

	if (maapi_apply_trans(sock, th, 0) != CONFD_OK)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_apply_transaction"));
	}

	if (maapi_close(sock) != CONFD_OK)
	{
		throw WifiManagerException(__FILE__, __LINE__, confd_lasterr() + string(": maapi_close"));
	}
}

void update_config(int cdbsock)
{
	struct wifi_config config;

	ok(cdb_start_session(cdbsock, CDB_RUNNING), "cdb_start_session")
	read_config(cdbsock, config);
	apply_config(config);
	cdb_end_session(cdbsock);
}

static int get_elem(struct confd_trans_ctx *tctx, confd_hkeypath_t * keypath)
{
	debug("Enter");
	const char *bssid_key = (char *)CONFD_GET_BUFPTR(&keypath->v[1][0]);
	unsigned i;
	const vector<ScanResult>& scan_results = *(vector<ScanResult> *)tctx->t_opaque;

	for(i = 0; i != scan_results.size(); ++i) { if (scan_results[i].bssid == bssid_key) break; }

	if (i == scan_results.size())
	{
		debug("not found");
		confd_data_reply_not_found(tctx);
		return CONFD_OK;
	}

	confd_value_t v;
	auto& scan_result = scan_results[i];

	switch (CONFD_GET_XMLTAG(&(keypath->v[0][0]))) {
	case rmf_ifs_bssid:
		debug("Returning bssid");
		CONFD_SET_STR(&v, scan_result.bssid.c_str());
		break;
	case rmf_ifs_frequency:
		debug("Returning frequency");
		CONFD_SET_INT32(&v, scan_result.frequency);
		break;
	case rmf_ifs_signal_level:
		debug("returning signal strength");
		CONFD_SET_INT32(&v, scan_result.signal_level);
		break;
	case rmf_ifs_flags:
		debug("returning flags (%s)", scan_result.flags.c_str());
		CONFD_SET_STR(&v, scan_result.flags.c_str());
		break;
	case rmf_ifs_ssid:
		debug("returning ssid");
		CONFD_SET_STR(&v, scan_result.ssid.c_str());
		break;
	default:
		return CONFD_ERR;
	}

	confd_data_reply_value(tctx, &v);

	return CONFD_OK;
}

static int get_next(struct confd_trans_ctx *tctx, confd_hkeypath_t * keypath __attribute__ ((unused)),
		long next)
{
	debug("Enter");
	static vector<ScanResult> scan_results;

	try
	{
		if (next == -1)
		{
			scan_results = supp.scan_results();
			tctx->t_opaque = &scan_results;
			next = 0;
		}

		if ((unsigned long)next == scan_results.size())
		{
			confd_data_reply_next_key(tctx, NULL, -1, -1);
			return CONFD_OK;
		}

		confd_value_t v;
		CONFD_SET_STR(&v, scan_results[next].bssid.c_str());
		confd_data_reply_next_key(tctx, &v, 1, ++next);

		return CONFD_OK;
	}
	catch(exception& e)
	{
		//TODO: log error
		cerr << e.what();
		return CONFD_ERR;
	}
}

static int s_init(struct confd_trans_ctx *tctx)
{
    confd_trans_set_fd(tctx, workersock);
    return CONFD_OK;
}

static int s_finish(struct confd_trans_ctx *tctx)
{
    return CONFD_OK;
}

static int init_connect_point(struct confd_user_info *uinfo)
{

	debug("Enter");

	confd_action_set_fd(uinfo, workersock);
	return CONFD_OK;
}

static void action_connect_thread(struct wifi_config config)
{
	int msock, th;

	start_transaction(msock, th);

	ok(maapi_cd(msock, th, "/wifi{1}"), "maapi_cd");

	confd_value_t v;

	CONFD_SET_STR(&v, config.station.ssid.c_str());
	write_leaf(msock, th, &v, "station/ssid");
	CONFD_SET_ENUM_VALUE(&v, rmf__wifiMode_station);
	write_leaf_enum(msock, th, "mode", rmf__wifiMode_station);

	finish_transaction(msock, th);
}


static int action_connect(struct confd_user_info *uinfo __attribute__ ((unused)),
		struct xml_tag *name __attribute__ ((unused)), confd_hkeypath_t * kp __attribute__ ((unused)),
		confd_tag_value_t * params __attribute__ ((unused)), int n __attribute__ ((unused)))
{
	debug("Enter tag %d", name->tag);

	int rtnval = CONFD_OK;
	confd_tag_value_t reply;
	char ssid[256];

	confd_pp_value(ssid, sizeof(ssid), CONFD_GET_TAG_VALUE(&params[0]));
	debug("Enter n=%d, ssid=%s", n, ssid);

//	new_config.station.ssid = ssid;

	switch (name->tag)
	{
		case wifi_connect:
			try
			{
#if 0
				thread(action_thread).detach();
				string msg("Connecting to ");
				msg += ssid;
				CONFD_SET_TAG_STR(reply, wifi_status, msg.c_str());
#endif
			}
			catch(exception& e)
			{
				debug("caught exception %s", e.what());
				CONFD_SET_TAG_STR(&reply, wifi_status, e.what());
			}
			confd_action_reply_values(uinfo, &reply, 1);
			break;
		default:
			confd_fatal("Got bad operation: %d", name->tag);
			break;
	}

	return rtnval;
}

void register_cbs()
{
	struct confd_data_cbs dcb;
	struct confd_trans_cbs trans;
	struct confd_action_cbs acb;

	memset(&trans, 0, sizeof (struct confd_trans_cbs));
	trans.init = s_init;
	trans.finish = s_finish;
	if (confd_register_trans_cb(dctx, &trans) == CONFD_ERR)
			confd_fatal("Failed to register trans cb \n");

	memset(&dcb, 0, sizeof(dcb));
	strcpy(dcb.callpoint, "wifi_scan_results");
	dcb.get_elem = get_elem;
	dcb.get_next = get_next;
	if (confd_register_data_cb(dctx, &dcb) != CONFD_OK)
		confd_fatal("Failed to register callpoint \"%s\"", dcb.callpoint);

	memset(&acb, 0, sizeof(acb));
	strcpy(acb.actionpoint, "connect-point");
	acb.init = init_connect_point;
	acb.action = action_connect;
	if (confd_register_action_cbs(dctx, &acb) != CONFD_OK)
		confd_fatal("Couldn't register action connect-point");

	debug("Registered callpoint");
}

int main(int argc, char **argv)
{
		struct sockaddr_in addr;
		int c, status, subsock, sock;
		int headpoint;
		enum confd_debug_level dbgl = CONFD_DEBUG;
		const char *confd_addr = "127.0.0.1";
		int confd_port = CONFD_PORT;
    struct confd_action_cbs acb;
		int ret;

		// Process command-line options
		while ((c = getopt(argc, argv, "dta:p:")) != EOF) {
				switch (c) {
				case 'd': dbgl = CONFD_DEBUG; break;
				case 't': dbgl = CONFD_TRACE; break;
				case 'a': confd_addr = optarg; break;
				case 'p': confd_port = atoi(optarg); break;
				}
		}

		// Initialize IP address of confd
		addr.sin_addr.s_addr = inet_addr(confd_addr);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(confd_port);

		confd_init(argv[0], stderr, dbgl);

		// Create wifi{1}
		make_wifi_node();

		init_daemon();

		// Register data callbacks
		register_cbs();
    if (confd_register_done(dctx) != CONFD_OK)
        confd_fatal("Failed to complete registration \n");

		if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
				confd_fatal("%s: Failed to create socket", argv[0]);

		if (confd_load_schemas((struct sockaddr*)&addr,
													 sizeof (struct sockaddr_in)) != CONFD_OK)
				confd_fatal("%s: Failed to load schemas from confd\n", argv[0]);

		if (cdb_connect(sock, CDB_DATA_SOCKET, (struct sockaddr *)&addr,
										sizeof(struct sockaddr_in)) != CONFD_OK)
				confd_fatal("%s: Failed to connect to ConfD", argv[0]);

		if ((subsock = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
				confd_fatal("Failed to open socket\n");

		if (cdb_connect(subsock, CDB_SUBSCRIPTION_SOCKET, (struct sockaddr*)&addr,
											sizeof (struct sockaddr_in)) < 0)
				confd_fatal("Failed to cdb_connect() to confd \n");

		/* setup subscription point */

		if ((status = cdb_subscribe(subsock, 3, wifi__ns, &headpoint,
																"/wifi{1}"))
				!= CONFD_OK) {
				confd_fatal("Terminate: subscribe %d\n", status);
		}
		if (cdb_subscribe_done(subsock) != CONFD_OK)
				confd_fatal("cdb_subscribe_done() failed");

		update_config(sock);

		while (1) {
				int status;
				struct pollfd set[3];

				set[0].fd = subsock;
				set[0].events = POLLIN;
				set[0].revents = 0;

        set[1].fd = ctlsock;
        set[1].events = POLLIN;
        set[1].revents = 0;

        set[2].fd = workersock;
        set[2].events = POLLIN;
        set[2].revents = 0;

				debug("Waiting for events: %d, %d, %d", set[0].fd, set[1].fd, set[2].fd);

				if (poll(set, 3, -1) < 0) {
						if (errno != EINTR) {
								perror("Poll failed:");
								continue;
						}
				}

				debug("Got event: %d, %d, %d", set[0].revents & POLLIN,
						set[1].revents & POLLIN, set[2].revents & POLLIN);

				if (set[0].revents & POLLIN) {
						debug("Handling POLLIN on subscription socket");
						int sub_points[1];
						int reslen;

						if ((status = cdb_read_subscription_socket(subsock,
																											 &sub_points[0],
																											 &reslen)) != CONFD_OK) {
								confd_fatal("terminate sub_read: %d\n", status);
						}
						if (reslen > 0) {
								fprintf(stderr, "*** Config updated \n");
						}

						update_config(sock);

						if ((status = cdb_sync_subscription_socket(subsock,
																											 CDB_DONE_PRIORITY))
								!= CONFD_OK) {
								confd_fatal("failed to sync subscription: %d\n", status);
						}

				}

        /* Check for I/O */
        if (set[1].revents & POLLIN) {
						debug("Handling POLLIN on control socket");
            if ((ret = confd_fd_ready(dctx, ctlsock)) == CONFD_EOF) {
                confd_fatal("Control socket closed\n");
            } else if (ret == CONFD_ERR && confd_errno != CONFD_ERR_EXTERNAL) {
                confd_fatal("Error on control socket request: %s (%d): %s\n",
                     confd_strerror(confd_errno), confd_errno, confd_lasterr());
            }
        }

        if (set[2].revents & POLLIN) {
						debug("Handling POLLIN on worker socket");
            if ((ret = confd_fd_ready(dctx, workersock)) == CONFD_EOF) {
                confd_fatal("Worker socket closed\n");
            } else if (ret == CONFD_ERR && confd_errno != CONFD_ERR_EXTERNAL) {
                confd_fatal("Error on worker socket request: %s (%d): %s\n",
                     confd_strerror(confd_errno), confd_errno, confd_lasterr());
            }
        }
		}
}

