/*
 * Copyright (C) 1994-2016 Altair Engineering, Inc.
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
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "portability.h"
#include "libpbs.h"
#include "dis.h"


/**
 * @brief
 *	This function sends the Submit Reservation request
 *
 * @param[in] c - socket descriptor
 * @param[in] resv_id - reservation identifier
 * @param[in] attrib - pointer to attribute list
 * @param[in] extend - extention string for req encode
 *
 * @return      string
 * @retval      resvn id	Success
 * @retval      NULL		error
 *
 */

char *
PBSD_submit_resv(int connect, char *resv_id, struct attropl *attrib, char *extend)
{
	struct batch_reply *reply;
	char  *return_resv_id = (char *)NULL;
	int    rc;
	int    sock;

	sock = connection[connect].ch_socket;
	DIS_tcp_setup(sock);

	/* first, set up the body of the Submit Reservation request */

	if ((rc = encode_DIS_ReqHdr(sock, PBS_BATCH_SubmitResv, pbs_current_user)) ||
		(rc = encode_DIS_SubmitResv(sock, resv_id, attrib)) ||
		(rc = encode_DIS_ReqExtend(sock, extend))) {
		connection[connect].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[connect].ch_errtxt == NULL) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		return return_resv_id;
	}
	if (DIS_tcp_wflush(sock)) {
		pbs_errno = PBSE_PROTOCOL;
		return return_resv_id;
	}

	/* read reply from stream into presentation element */

	reply = PBSD_rdrpy(connect);
	if (reply == NULL) {
		pbs_errno = PBSE_PROTOCOL;
	} else if (!pbs_errno && reply->brp_choice &&
		reply->brp_choice != BATCH_REPLY_CHOICE_Text) {
		pbs_errno = PBSE_PROTOCOL;
	} else if (connection[connect].ch_errno == 0 && reply->brp_code == 0) {
		if (reply->brp_choice == BATCH_REPLY_CHOICE_Text) {
			return_resv_id = strdup(reply->brp_un.brp_txt.brp_str);
			if (return_resv_id == NULL) {
				pbs_errno = PBSE_SYSTEM;
			}
		}
	}

	PBSD_FreeReply(reply);
	return return_resv_id;
}