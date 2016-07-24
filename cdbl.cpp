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
#include "wifi.h"
#include "WpaSupplicant.h"

using namespace std;

WpaSupplicant supp;

#if 0
static int signaled = 0;

static void sighdlr(int sig)
{
		signaled++;
}
#endif


/* my internal db */

#define MAXH 3
#define MAXC 2

struct child {
		int64_t dn;
		char childattr[255];
		int inuse;
};

struct rfhead {
		int64_t dn;
		char sector_id[255];
		struct child children[MAXC];
		int inuse;
};
struct rfhead rfheads[MAXH];

static struct config
{
	int mode = wifi_mode_off;

	struct
	{
		string ssid;
		string psk;
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
	cout << "BEGIN CONFIG: " << title << endl;
	cout << "------------" << endl;
	cout << "mode: " << config.mode << endl;
	cout << "station.ssid: " << config.station.ssid << endl;
	cout << "station.psk: " << config.station.psk << endl;
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
	else if (confd_errno == CONFD_ERR_NOEXISTS)
	{
		str.clear();
	}
	else
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
		else                              confd_fatal("Unexpected type: %d", val.type);
	}
	else if (confd_errno == CONFD_ERR_NOEXISTS)
	{
		n = 0;
	}
	else
	{
		confd_fatal("cdb_get failed on %s: %s", path, confd_lasterr());
	}
}

static void read_config(int cdbsock, struct config& config)
{
	if (cdb_start_session(cdbsock, CDB_RUNNING) != CONFD_OK)
	{
		confd_fatal("Cannot start session: %s\n", confd_lasterr());
	}

	if (cdb_set_namespace(cdbsock, wifi__ns) != CONFD_OK)
	{
		confd_fatal("Cannot set namespace to wifi__ns: %s", confd_lasterr());
	}
#if 0
	ret = cdb_get_enum_value(cdbsock, &new_config.mode, "/wifi/mode");
	if (ret == CONFD_ERR && confd_errno != CONFD_ERR_NOEXISTS)
	{
		confd_fatal("cdb_get_enum_value failed on /wifi/mode: %s", confd_lasterr());
	}
#endif

	read_leaf(cdbsock , "/wifi/mode"              , new_config.mode              ) ;
	read_leaf(cdbsock , "/wifi/station/ssid"      , new_config.station.ssid      ) ;
	read_leaf(cdbsock , "/wifi/station/psk"       , new_config.station.psk       ) ;
	read_leaf(cdbsock , "/wifi/access_point/ssid" , new_config.access_point.ssid ) ;
	read_leaf(cdbsock , "/wifi/access_point/psk"  , new_config.access_point.psk  ) ;

	cdb_end_session(cdbsock);

	print_config("read_config", config);
}

static void apply_config(struct config& config)
{
	print_config("apply_config", config);

	if (config.mode == wifi_mode_off)
	{
		supp.remove_all_networks();
		return;
	}

	if (config.mode == wifi_mode_station)
	{
		if (!config.station.ssid.empty())
		{
			supp.connect(config.station.ssid, config.station.psk);
		}
		return;
	}
}

int main(int argc, char **argv)
{
		struct sockaddr_in addr;
		int c, status, subsock, sock;
		int headpoint;
		enum confd_debug_level dbgl = CONFD_SILENT;
		const char *confd_addr = "127.0.0.1";
		int confd_port = CONFD_PORT;

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
																"/wifi"))
				!= CONFD_OK) {
				confd_fatal("Terminate: subscribe %d\n", status);
		}
		if (cdb_subscribe_done(subsock) != CONFD_OK)
				confd_fatal("cdb_subscribe_done() failed");

		/* initialize db */
		cout << "Calling read_config" << endl;
		read_config(sock, current_config);
		apply_config(current_config);
		//dump_db();

		/* "interactive" feature, catch SIGINT and dump db to stderr */
//		signal(SIGINT, sighdlr);

		while (1) {
				int status;
				struct pollfd set[1];

				set[0].fd = subsock;
				set[0].events = POLLIN;
				set[0].revents = 0;

				if (poll(&set[0], sizeof(set)/sizeof(*set), -1) < 0) {
						if (errno != EINTR) {
								perror("Poll failed:");
								continue;
						}
				}

				if (set[0].revents & POLLIN) {
						int sub_points[1];
						int reslen;

						if ((status = cdb_read_subscription_socket(subsock,
																											 &sub_points[0],
																											 &reslen)) != CONFD_OK) {
								confd_fatal("terminate sub_read: %d\n", status);
						}
						if (reslen > 0) {
								fprintf(stderr, "*** Config updated \n");

#if 0
								if ((status = cdb_start_session(sock,CDB_RUNNING)) != CONFD_OK)
										confd_fatal("Cannot start session\n");
								if ((status = cdb_set_namespace(sock, wifi__ns)) != CONFD_OK)
										confd_fatal("Cannot set namespace\n");
								cdb_diff_iterate(subsock, sub_points[0], iter,
																 ITER_WANT_PREV, (void*)&sock);
								cdb_end_session(sock);
#endif


								/* Here is an alternative approach to checking a subtree */
								/* the function below will invoke cdb_diff_iterate */
								/* and check if any changes have beem made in the tagpath */
								/* described by tags[] */
								/* This still only applies to the subscription point which */
								/* is being used */
#if 0

								struct xml_tag tags[] = {{root_root, root__ns},
																				 {root_NodeB, root__ns},
																				 {root_RFHead, root__ns},
																				 {root_Child, root__ns}};
								int tagslen = sizeof(tags)/sizeof(tags[0]);
								/* /root/NodeB/RFHead/Child */
								int retv = cdb_diff_match(subsock, sub_points[0],
																					tags, tagslen);
								fprintf(stderr, "Diff match: %s\n", retv ? "yes" : "no");
#endif
						}

						read_config(sock, new_config);
						apply_config(new_config);

						if ((status = cdb_sync_subscription_socket(subsock,
																											 CDB_DONE_PRIORITY))
								!= CONFD_OK) {
								confd_fatal("failed to sync subscription: %d\n", status);
						}
						//dump_db();
				}
#if 0
				if (signaled) {  /* dump db to stderr when user hits Ctrl-C */
						//dump_db();
						signaled = 0;
				}
#endif
		}
}

