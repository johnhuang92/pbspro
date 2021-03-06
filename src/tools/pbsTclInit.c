/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */
/**
 * @file    pbsTclInit.c
 *
 * @brief
 *		pbsTclInit - All the pbs specific code needed to make pbstclsh
 *		work is here.  The routine pbsTcl_Init() is to be called
 *		in place of Tcl_Init().
 *
 * Functions included are:
 * 	pbsTcl_Init()
 * 	main()
 */

#include	"pbs_version.h"
#include	"pbs_config.h"
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	"tcl.h"
#include	"rm.h"
#include	"pbs_ifl.h"
#include	"pbs_internal.h"
#include	"log.h"
#include	"rpp.h"



char	log_buffer[4096];
#ifdef NAS /* localmod 099 */
extern	int	quiet;
#endif /* localmod 099 */

extern	void	add_cmds(Tcl_Interp *interp);

#define SHOW_NONE 0xff
int log_mask;

void
log_rppfail(char *mess)
{
	fprintf(stderr, "rpp error: %s\n", mess);
}

void
log_tppmsg(int level, const char *objname, char *mess)
{
	if ((level | log_mask) <= LOG_ERR)
		fprintf(stderr, "rpp error: %s\n", mess);
}

/**
 * @brief
 * 		pbsTcl_Init	- Function to initialize Tcl interpreter based on the environment.
 *
 * @param[in,out]	interp	-	Interpreter for application.
 *
 * @return	int
 * @retval	TCL_OK	: everything looks good.
 * @retval	TCL_ERROR	: something got wrong!
 */
int
pbsTcl_Init(Tcl_Interp *interp)
{
	if (Tcl_Init(interp) == TCL_ERROR)
		return TCL_ERROR;
#if	TCLX
	if (Tclx_Init(interp) == TCL_ERROR)
		return TCL_ERROR;
#endif

	fullresp(0);
	add_cmds(interp);

	Tcl_SetVar(interp, "tcl_rcFileName", "~/.tclshrc", TCL_GLOBAL_ONLY);
	return TCL_OK;
}
/**
 * @brief
 * 		main - the entry point in pbsTclInit.c
 *
 * @param[in]	argc	-	argument count.
 * @param[in]	argv	-	argument variables.
 *
 * @return	int
 * @retval	0	: success
 */
int
main(int argc, char *argv[])
{
	char	tcl_libpath[MAXPATHLEN+13];	/* 13 for "TCL_LIBRARY=" + \0 */
	int rc;

	/*the real deal or just pbs_version and exit?*/

	execution_mode(argc, argv);
	if(set_msgdaemonname("pbs_tclsh")) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	set_logfile(stderr);

#ifdef WIN32
	winsock_init();
	Tcl_FindExecutable(argv[0]);
#endif

	/* load the pbs conf file */
	if (pbs_loadconf(0) == 0) {
		fprintf(stderr, "%s: Configuration error\n", argv[0]);
		return (1);
	}

	if (!getenv("TCL_LIBRARY")) {
		if (pbs_conf.pbs_exec_path) {
			sprintf((char *)tcl_libpath,
#ifdef WIN32
				"TCL_LIBRARY=%s/lib/tcl%s",
#else
				"TCL_LIBRARY=%s/tcltk/lib/tcl%s",
#endif
				pbs_conf.pbs_exec_path, TCL_VERSION);
			putenv(tcl_libpath);
		}
	}
	if (pbs_conf.pbs_use_tcp == 1) {
		char 			*nodename;
		struct			tpp_config tpp_conf;
		char			my_hostname[PBS_MAXHOSTNAME+1];
		fd_set 			selset;
		struct 			timeval tv;

		if (pbs_conf.pbs_leaf_name)
			nodename = pbs_conf.pbs_leaf_name;
		else {
			if (gethostname(my_hostname, (sizeof(my_hostname) - 1)) < 0) {
				fprintf(stderr, "Failed to get hostname\n");
				return -1;
			}
			nodename = my_hostname;
		}

		/* We don't want to show logs related to connecting pbs_comm on console
		 * this set this flag to ignore it
		 */
		log_mask = SHOW_NONE;

		/* set tpp function pointers */
		set_tpp_funcs(log_tppmsg);

		/* call tpp_init */
		rc = 0;
#ifndef WIN32
		if (pbs_conf.auth_method == AUTH_MUNGE)
			rc = set_tpp_config(&pbs_conf, &tpp_conf, nodename, -1, pbs_conf.pbs_leaf_routers,
								pbs_conf.pbs_use_compression,
								TPP_AUTH_EXTERNAL,
								get_ext_auth_data, validate_ext_auth_data);
		else
#endif
			rc = set_tpp_config(&pbs_conf, &tpp_conf, nodename, -1, pbs_conf.pbs_leaf_routers,
								pbs_conf.pbs_use_compression,
								TPP_AUTH_RESV_PORT,
								NULL, NULL);
		if (rc == -1) {
			fprintf(stderr, "Error setting TPP config\n");
			return -1;
		}

		if ((rpp_fd = tpp_init(&tpp_conf)) == -1) {
			fprintf(stderr, "rpp_init failed\n");
			return -1;
		}

		/*
		 * Wait for net to get restored, ie, app to connect to routers
		 */
		FD_ZERO(&selset);
		FD_SET(rpp_fd, &selset);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		select(FD_SETSIZE, &selset, (fd_set *) 0, (fd_set *) 0, &tv);

		rpp_poll(); /* to clear off the read notification */

		/* Once the connection is established we can unset log_mask */
		log_mask &= ~SHOW_NONE;
	} else {
		/* set rpp function pointers */
		set_rpp_funcs(log_rppfail);
	}
	Tcl_Main(argc, argv, pbsTcl_Init);
	return 0;
}
