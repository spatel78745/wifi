/*
 * Copyright 2007 Tail-F Systems AB
 */

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

WpaSupplicant& supp = WpaSupplicant::getInstance();

static int ctlsock;
static int workersock;
static struct confd_daemon_ctx *dctx;

static struct config
{
	int mode = rmf__wifiMode_off;

	struct
	{
		string ssid;
		string psk;
		int ip_address_src;
	} station;

	struct
	{
		string ssid;
		string psk;
	} access_point;
} current_config, new_config;

bool operator==(const struct config& lhs, const struct config& rhs)
{
	return (   lhs.mode              == rhs.mode
					&& lhs.station.ssid      == rhs.station.ssid
					&& lhs.station.psk       == rhs.station.psk
					&& lhs.access_point.ssid == rhs.access_point.ssid
					&& lhs.access_point.psk  == rhs.access_point.psk
				 );
}

bool operator!=(const struct config& lhs, const struct config& rhs)
{
	return !(lhs == rhs);
}

static void print_config(const char *title, const struct config& config)
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

void read_leaf(int cdbsock, const char *path, string& str)
{
	confd_value_t val;

	int ret = cdb_get(cdbsock, &val, path);
	if (ret == CONFD_OK)
	{
		str = (char *)CONFD_GET_BUFPTR(&val);
		confd_free_value(&val);
	}
	else if (confd_errno != CONFD_ERR_NOEXISTS)
	{
		confd_fatal("cdb_get failed on %s: %s", path, confd_lasterr());
	}
}

void read_leaf(int cdbsock, const char *path, int& n)
{
	confd_value_t val;

	int ret = cdb_get(cdbsock, &val, path);
	if (ret == CONFD_OK)
	{
		if      (val.type == C_INT32)      n = CONFD_GET_INT32(&val);
		else if (val.type == C_ENUM_VALUE) n = CONFD_GET_ENUM_VALUE(&val);
		else                               confd_fatal("Unexpected type: %d", val.type);
	}
	else if (confd_errno != CONFD_ERR_NOEXISTS)
	{
		confd_fatal("cdb_get failed on %s: %s", path, confd_lasterr());
	}
}

static void read_config(int cdbsock, struct config& config)
{
	if (cdb_set_namespace(cdbsock, rmf__ns) != CONFD_OK)
	{
		confd_fatal("Cannot set namespace to wifi__ns: %s", confd_lasterr());
	}

	int ret;

	ret = cdb_cd(cdbsock, "%s[0]", WIFI_DIR);
	if (ret != CONFD_OK)
	{
		confd_fatal("cdb_cd failed (%s)", confd_lasterr());
	}

	read_leaf(cdbsock , "mode"                   , config.mode                   ) ;
	read_leaf(cdbsock , "station/ssid"           , config.station.ssid           ) ;
	read_leaf(cdbsock , "station/psk"            , config.station.psk            ) ;
	read_leaf(cdbsock , "station/ip-address-src" , config.station.ip_address_src ) ;
	read_leaf(cdbsock , "access_point/ssid"      , config.access_point.ssid      ) ;
	read_leaf(cdbsock , "access_point/psk"       , config.access_point.psk       ) ;

	print_config("read_config", config);
}

static void apply_config(struct config& config)
{
	print_config("apply_config", config);

	if (config.mode == rmf__wifiMode_off)
	{
		supp.remove_all_networks();
		return;
	}

	if (config.mode == rmf__wifiMode_station)
	{
		if (!config.station.ssid.empty())
		{
			supp.connect(config.station.ssid, config.station.psk);
		}
		return;
	}
}

#if 0
void write_leaf(int msock, int th, const char *path, string& val)
{
	int ret;

	ret = maapi_exists(msock, th, path);
	if (confd_errno != CONFD_OK)
	{
		confd_fatal("%s:%d Failed to write %s to %s err=%s", __func__, __LINE__,
				val.c_str(), path, confd_lasterr());
	}

	if (!ret)
	{
		ret = maapi_create(msock, th, path);
		if (ret != CONFD_OK)
		{
			confd_fatal("maapi_create failed on %s", path);
		}
	}

	confd_value_t v;

	CONFD_SET_STR(&v, val.c_str());
	ret = maapi_set_elem(msock, th, &v, path);
	if (ret != CONFD_OK)
	{
		confd_fatal("maapi_set_elem failed on %s", path);
	}
}

void write_leaf_enum(int msock, int th, const char *path, int val)
{
	int ret;

	ret = maapi_exists(msock, th, path);
	if (confd_errno != CONFD_OK)
	{
		confd_fatal("%s:%d Failed to write %d to %s err=%s", __func__, __LINE__,
				val, path, confd_lasterr());
	}

	if (!ret)
	{
		ret = maapi_create(msock, th, path);
		if (ret != CONFD_OK)
		{
			confd_fatal("maapi_create failed on %s", path);
		}
	}

	confd_value_t v;

	CONFD_SET_ENUM_VALUE(&v, val);
	ret = maapi_set_elem(msock, th, &v, path);
	if (ret != CONFD_OK)
	{
		confd_fatal("maapi_set_elem failed on %s", path);
	}
}
#endif

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
	// Create a socket
	int sock;

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
			confd_fatal("Failed to open socket\n");

	// Create a maapi connection to confd
	struct sockaddr_in addr;

	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFD_PORT);

	if (maapi_connect(sock, (struct sockaddr*)&addr,
										sizeof (struct sockaddr_in)) < 0)
			confd_fatal("Failed to confd_connect() to confd \n");

	// Start an admin-user session
		struct confd_ip ip;
		const char *groups[] = {"admin"};

    ip.af = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &ip.ip.v4);

    if (maapi_start_user_session(sock, "admin", "system", groups, 1, &ip, CONFD_PROTO_TCP) != CONFD_OK)
		{
			confd_fatal("Failed to start user session");
		}

	// Start a transaction
	int tid;

	if ((tid = maapi_start_trans(sock, CONFD_RUNNING, CONFD_READ_WRITE)) < 0)
			confd_fatal("failed to start trans \n");

	// Create wifi{0}
	int ret = maapi_create(sock, tid, "/wifi{1}");
	if (ret != CONFD_OK && confd_errno != CONFD_ERR_ALREADY_EXISTS)
	{
		confd_fatal("Failed to maapi_create /wifi{1}: %s", confd_lasterr());
	}

	// Apply the transaction
	if (maapi_apply_trans(sock, tid, 0) != CONFD_OK)
	{
		confd_fatal("Failed to maapi_apply_trans: %s", confd_lasterr());
	}

	// Close the socket
	if (maapi_close(sock) != CONFD_OK)
	{
		confd_fatal("Failed to maapi_close: %s", confd_lasterr());
	}
}

void register_actions()
{
}

void update_config(int cdbsock)
{
	int ret;

	if ((ret = cdb_start_session(cdbsock, CDB_RUNNING)) != CONFD_OK)
			confd_fatal("Cannot start session\n");
	read_config(cdbsock, current_config);
	apply_config(current_config);
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

void register_cbs()
{
	struct confd_data_cbs dcb;
	struct confd_trans_cbs trans;

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

		print_config("main", current_config);

		while ((c = getopt(argc, argv, "dta:p:")) != EOF) {
				switch (c) {
				case 'd': dbgl = CONFD_DEBUG; break;
				case 't': dbgl = CONFD_TRACE; break;
				case 'a': confd_addr = optarg; break;
				case 'p': confd_port = atoi(optarg); break;
				}
		}

		addr.sin_addr.s_addr = inet_addr(confd_addr);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(confd_port);

		confd_init(argv[0], stderr, dbgl);

		make_wifi_node();

		init_daemon();

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

