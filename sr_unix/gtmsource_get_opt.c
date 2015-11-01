/****************************************************************
 *								*
 *	Copyright 2006 Fidelity Information Services, Inc.*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_stdlib.h"
#include "gtm_netdb.h"
#include "gtm_socket.h"
#include "gtm_inet.h"
#include "gtm_string.h"
#include "gtm_ctype.h"
#if !defined(__MVS__) && !defined(VMS)
#include <sys/socketvar.h>
#endif
#ifdef VMS
#include <descrip.h> /* Required for gtmsource.h */
#endif
#include <errno.h>

#include "gdsroot.h"
#include "gdsblk.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "repl_msg.h"
#include "jnl.h"
#include "gtmsource.h"
#include "cli.h"
#include "gtm_stdio.h"
#include "util.h"
#include "repl_log.h"
#include "gtmmsg.h"		/* for "gtm_putmsg" prototype */
#include "gtm_logicals.h"	/* for GTM_REPL_INSTSECONDARY */
#include "trans_log_name.h"
#include "iosp.h"		/* for SS_NORMAL */

#define MAX_SECONDARY_LEN 	(MAX_HOST_NAME_LEN + 11) /* +11 for ':' and
							  * port number */

#define DEFAULT_JNLPOOL_SIZE		(64 * 1024 * 1024)

#define DEFAULT_SHUTDOWN_TIMEOUT	30

#define GTMSOURCE_CONN_PARMS_LEN ((10 + 1) * GTMSOURCE_CONN_PARMS_COUNT - 1)

GBLREF	gtmsource_options_t	gtmsource_options;

int gtmsource_get_opt(void)
{
	boolean_t	secondary, dotted_notation, log, log_interval_specified, buffsize_status, filter, connect_parms_badval;
	char		*connect_parm_token_str, *connect_parm;
	char		*connect_parms_str, tmp_connect_parms_str[GTMSOURCE_CONN_PARMS_LEN + 1];
	char		secondary_sys[MAX_SECONDARY_LEN], *c, inst_name[MAX_FN_LEN + 1];
	char		statslog_val[4]; /* "ON" or "OFF" */
	char		update_val[sizeof("DISABLE")]; /* "ENABLE" or "DISABLE" */
	int		tries, index = 0, timeout_status, connect_parms_index;
	mstr		log_nam, trans_name;
	struct hostent	*sec_hostentry;
	unsigned short	log_file_len, filter_cmd_len;
	unsigned short	secondary_len, inst_name_len, statslog_val_len, update_val_len, connect_parms_str_len;

	error_def(ERR_REPLINSTSECLEN);
	error_def(ERR_REPLINSTSECUNDF);

	memset((char *)&gtmsource_options, 0, sizeof(gtmsource_options));
	gtmsource_options.start = (CLI_PRESENT == cli_present("START"));
	gtmsource_options.shut_down = (CLI_PRESENT == cli_present("SHUTDOWN"));
	gtmsource_options.activate = (CLI_PRESENT == cli_present("ACTIVATE"));
	gtmsource_options.deactivate = (CLI_PRESENT == cli_present("DEACTIVATE"));
	gtmsource_options.checkhealth = (CLI_PRESENT == cli_present("CHECKHEALTH"));
	gtmsource_options.statslog = (CLI_PRESENT == cli_present("STATSLOG"));
	gtmsource_options.showbacklog = (CLI_PRESENT == cli_present("SHOWBACKLOG"));
	gtmsource_options.changelog = (CLI_PRESENT == cli_present("CHANGELOG"));
	gtmsource_options.stopsourcefilter = (CLI_PRESENT == cli_present("STOPSOURCEFILTER"));
	gtmsource_options.needrestart = (CLI_PRESENT == cli_present("NEEDRESTART"));
	gtmsource_options.losttncomplete = (CLI_PRESENT == cli_present("LOSTTNCOMPLETE"));
	gtmsource_options.jnlpool = (CLI_PRESENT == cli_present("JNLPOOL"));
	secondary = (CLI_PRESENT == cli_present("SECONDARY"));
	gtmsource_options.rootprimary = ROOTPRIMARY_UNSPECIFIED; /* to indicate unspecified state */
	if (CLI_PRESENT == cli_present("ROOTPRIMARY"))
		gtmsource_options.rootprimary = ROOTPRIMARY_SPECIFIED;
	else if (CLI_PRESENT == cli_present("PROPAGATEPRIMARY"))
		gtmsource_options.rootprimary = PROPAGATEPRIMARY_SPECIFIED;
	else
	{	/* Neither ROOTPRIMARY nor PROPAGATEPRIMARY specified. Assume default values.
		 * Assume ROOTPRIMARY for -START -SECONDARY (active source server start) and -ACTIVATE commands.
		 * Assume PROPAGATEPRIMARY for -START -PASSIVE (passive source server start) and -DEACTIVATE commands.
		 */
		if ((gtmsource_options.start && secondary) || gtmsource_options.activate)
			gtmsource_options.rootprimary = ROOTPRIMARY_SPECIFIED;
		if ((gtmsource_options.start && !secondary) || gtmsource_options.deactivate)
			gtmsource_options.rootprimary = PROPAGATEPRIMARY_SPECIFIED;
	}
	gtmsource_options.instsecondary = (CLI_PRESENT == cli_present("INSTSECONDARY"));
	if (gtmsource_options.instsecondary)
	{	/* -INSTSECONDARY is specified in the command line. */
		inst_name_len = sizeof(inst_name);;
		if (!cli_get_str("INSTSECONDARY", &inst_name[0], &inst_name_len))
		{
			util_out_print("Error parsing INSTSECONDARY qualifier", TRUE);
			return(-1);
		}
	} else
	{	/* Check if environment variable "gtm_repl_instsecondary" is defined.
		 * Do that only if any of the following qualifiers is present as these are the only ones that honour it.
		 * 	Mandatory : START, ACTIVATE, DEACTIVATE, STOPSOURCEFILTER, CHANGELOG, STATSLOG, NEEDRESTART,
		 *	Optional  : CHECKHEALTH, SHOWBACKLOG or SHUTDOWN
		 */
		if (gtmsource_options.start || gtmsource_options.activate || gtmsource_options.deactivate
			|| gtmsource_options.stopsourcefilter || gtmsource_options.changelog
			|| gtmsource_options.statslog || gtmsource_options.needrestart
			|| gtmsource_options.checkhealth || gtmsource_options.showbacklog || gtmsource_options.shut_down)
		{
			log_nam.addr = GTM_REPL_INSTSECONDARY;
			log_nam.len = sizeof(GTM_REPL_INSTSECONDARY) - 1;
			trans_name.addr = &inst_name[0];
			if (SS_NORMAL == trans_log_name(&log_nam, &trans_name, inst_name))
			{
				gtmsource_options.instsecondary = TRUE;
				inst_name_len = trans_name.len;
			} else if (!gtmsource_options.checkhealth && !gtmsource_options.showbacklog && !gtmsource_options.shut_down)
			{
				gtm_putmsg(VARLSTCNT(1) ERR_REPLINSTSECUNDF);
				return (-1);
			}
		}
	}
	if (gtmsource_options.instsecondary)
	{	/* Secondary instance name specified either through -INSTSECONDARY or "gtm_repl_instsecondary" */
		inst_name[inst_name_len] = '\0';
		if ((MAX_INSTNAME_LEN <= inst_name_len) || (0 == inst_name_len))
		{
			gtm_putmsg(VARLSTCNT(4) ERR_REPLINSTSECLEN, 2, inst_name_len, inst_name);
			return (-1);
		}
		assert((inst_name_len + 1) <= MAX_INSTNAME_LEN);
		memcpy(gtmsource_options.secondary_instname, inst_name, inst_name_len + 1);	/* copy terminating '\0' as well */
	}
	if (gtmsource_options.start || gtmsource_options.activate)
	{
		if (secondary)
		{
			secondary_len = MAX_SECONDARY_LEN;
			if (!cli_get_str("SECONDARY", secondary_sys, &secondary_len))
			{
				util_out_print("Error parsing SECONDARY qualifier", TRUE);
				return(-1);
			}
			/* Parse secondary_sys into secondary_host
			 * and secondary_port */
			c = secondary_sys;
			dotted_notation = TRUE;
			while(*c && *c != ':')
			{
				if ('.' != *c && !ISDIGIT(*c))
					dotted_notation = FALSE;
				gtmsource_options.secondary_host[index++] = *c++;
			}
			gtmsource_options.secondary_host[index] = '\0';
			if (':' != *c)
			{
				util_out_print("Secondary port number should be specified", TRUE);
				return(-1);
			}
			errno = 0;
			if (((0 == (gtmsource_options.secondary_port = ATOI(++c))) && (0 != errno))
				|| (0 >= gtmsource_options.secondary_port))
			{
				util_out_print("Error parsing secondary port number !AD", TRUE, LEN_AND_STR(c));
				return(-1);
			}
			/* Validate the specified secondary host name */
			if (dotted_notation)
			{
				if ((in_addr_t)-1 ==
					(gtmsource_options.sec_inet_addr = INET_ADDR(gtmsource_options.secondary_host)))
				{
					util_out_print("Invalid IP address !AD", TRUE,
						LEN_AND_STR(gtmsource_options.secondary_host));
					return(-1);
				}
			} else
			{
				for (tries = 0;
		     	     	     tries < MAX_GETHOST_TRIES &&
		     	     	     !(sec_hostentry = GETHOSTBYNAME(gtmsource_options.secondary_host)) &&
		     	     	     h_errno == TRY_AGAIN;
		     	     	     tries++);
				if (NULL == sec_hostentry)
				{
					util_out_print("Could not find IP address for !AD", TRUE,
						LEN_AND_STR(gtmsource_options.secondary_host));
					return(-1);
				}
				gtmsource_options.sec_inet_addr = ((struct in_addr *)sec_hostentry->h_addr_list[0])->s_addr;
			}
		}
		if (CLI_PRESENT == cli_present("CONNECTPARAMS"))
		{
			connect_parms_str_len = GTMSOURCE_CONN_PARMS_LEN + 1;
			if (!cli_get_str("CONNECTPARAMS", tmp_connect_parms_str, &connect_parms_str_len))
			{
				util_out_print("Error parsing CONNECTPARAMS qualifier", TRUE);
				return(-1);
			}
#ifdef VMS
			/* strip the quotes around the string. (DCL doesn't do it) */
			assert('"' == tmp_connect_parms_str[0]);
			assert('"' == tmp_connect_parms_str[connect_parms_str_len - 1]);
			connect_parms_str = &tmp_connect_parms_str[1];
			tmp_connect_parms_str[connect_parms_str_len - 1] = '\0';
#else
			connect_parms_str = &tmp_connect_parms_str[0];
#endif
			for (connect_parms_index =
					GTMSOURCE_CONN_HARD_TRIES_COUNT,
			     connect_parms_badval = FALSE,
			     connect_parm_token_str = connect_parms_str;
			     !connect_parms_badval &&
			     connect_parms_index < GTMSOURCE_CONN_PARMS_COUNT &&
			     (connect_parm = strtok(connect_parm_token_str,
						    GTMSOURCE_CONN_PARMS_DELIM))
			     		   != NULL;
			     connect_parms_index++,
			     connect_parm_token_str = NULL)

			{
				errno = 0;
				if ((0 == (gtmsource_options.connect_parms[connect_parms_index] = ATOI(connect_parm))
						&& 0 != errno) || 0 >= gtmsource_options.connect_parms[connect_parms_index])
					connect_parms_badval = TRUE;
			}
			if (connect_parms_badval)
			{
				util_out_print("Error parsing or invalid value parameter in CONNECTPARAMS", TRUE);
				return(-1);
			}
			if (GTMSOURCE_CONN_PARMS_COUNT != connect_parms_index)
			{
				util_out_print(
					"All CONNECTPARAMS - HARD TRIES, HARD TRIES PERIOD, "
					"SOFT TRIES PERIOD, "
					"ALERT TIME, HEARTBEAT INTERVAL, "
					"MAX HEARBEAT WAIT should be specified", TRUE);
				return(-1);
			}
		} else
		{
			gtmsource_options.connect_parms[GTMSOURCE_CONN_HARD_TRIES_COUNT] = REPL_CONN_HARD_TRIES_COUNT;
			gtmsource_options.connect_parms[GTMSOURCE_CONN_HARD_TRIES_PERIOD] = REPL_CONN_HARD_TRIES_PERIOD;
			gtmsource_options.connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD] = REPL_CONN_SOFT_TRIES_PERIOD;
			gtmsource_options.connect_parms[GTMSOURCE_CONN_ALERT_PERIOD] = REPL_CONN_ALERT_ALERT_PERIOD;
			gtmsource_options.connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD] = REPL_CONN_HEARTBEAT_PERIOD;
			gtmsource_options.connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT] = REPL_CONN_HEARTBEAT_MAX_WAIT;
		}
		if (gtmsource_options.connect_parms[GTMSOURCE_CONN_ALERT_PERIOD]<
				gtmsource_options.connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD])
			gtmsource_options.connect_parms[GTMSOURCE_CONN_ALERT_PERIOD] =
				gtmsource_options.connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD];
		if (gtmsource_options.connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT] <
				gtmsource_options.connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD])
			gtmsource_options.connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT] =
				gtmsource_options.connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD];
	}
	if (gtmsource_options.start || gtmsource_options.statslog || gtmsource_options.changelog || gtmsource_options.activate)
	{
		log = (cli_present("LOG") == CLI_PRESENT);
		log_interval_specified = (CLI_PRESENT == cli_present("LOG_INTERVAL"));
		if (log)
		{
			log_file_len = MAX_FN_LEN + 1;
			if (!cli_get_str("LOG", gtmsource_options.log_file, &log_file_len))
			{
				util_out_print("Error parsing LOG qualifier", TRUE);
				return(-1);
			}
		} else
			gtmsource_options.log_file[0] = '\0';
		gtmsource_options.src_log_interval = 0;
		if (log_interval_specified)
		{
			if (!cli_get_num("LOG_INTERVAL", (int4 *)&gtmsource_options.src_log_interval))
			{
				util_out_print("Error parsing LOG_INTERVAL qualifier", TRUE);
				return (-1);
			}
		}
		if (gtmsource_options.start && 0 == gtmsource_options.src_log_interval)
			gtmsource_options.src_log_interval = LOGTRNUM_INTERVAL;
		/* For changelog/activate, interval == 0 implies don't change log interval already established */
		/* We ignore interval specification for statslog, Vinaya 2005/02/07 */
	}
	if (gtmsource_options.start)
	{
		assert(secondary || CLI_PRESENT == cli_present("PASSIVE"));
		gtmsource_options.mode = ((secondary) ? GTMSOURCE_MODE_ACTIVE : GTMSOURCE_MODE_PASSIVE);
		if (buffsize_status = (CLI_PRESENT == cli_present("BUFFSIZE")))
		{
			if (!cli_get_int("BUFFSIZE", &gtmsource_options.buffsize))
			{
				util_out_print("Error parsing BUFFSIZE qualifier", TRUE);
				return(-1);
			}
			if (MIN_JNLPOOL_SIZE > gtmsource_options.buffsize)
				gtmsource_options.buffsize = MIN_JNLPOOL_SIZE;
		} else
			gtmsource_options.buffsize = DEFAULT_JNLPOOL_SIZE;
		/* Round up buffsize to the nearest (~JNL_WRT_END_MASK + 1) multiple */
		gtmsource_options.buffsize = ((gtmsource_options.buffsize + ~JNL_WRT_END_MASK) & JNL_WRT_END_MASK);
		if (filter = (CLI_PRESENT == cli_present("FILTER")))
		{
			filter_cmd_len = MAX_FILTER_CMD_LEN;
	    		if (!cli_get_str("FILTER", gtmsource_options.filter_cmd, &filter_cmd_len))
			{
				util_out_print("Error parsing FILTER qualifier", TRUE);
				return(-1);
			}
		} else
			gtmsource_options.filter_cmd[0] = '\0';
	}
	if (gtmsource_options.shut_down)
	{
		if ((timeout_status = cli_present("TIMEOUT")) == CLI_PRESENT)
		{
			if (!cli_get_int("TIMEOUT", &gtmsource_options.shutdown_time))
			{
				util_out_print("Error parsing TIMEOUT qualifier", TRUE);
				return(-1);
			}
			if (DEFAULT_SHUTDOWN_TIMEOUT < gtmsource_options.shutdown_time || 0 > gtmsource_options.shutdown_time)
			{
				gtmsource_options.shutdown_time = DEFAULT_SHUTDOWN_TIMEOUT;
				util_out_print("shutdown TIMEOUT changed to !UL", TRUE, gtmsource_options.shutdown_time);
			}
		} else if (CLI_NEGATED == timeout_status)
			gtmsource_options.shutdown_time = -1;
		else /* TIMEOUT not specified */
			gtmsource_options.shutdown_time = DEFAULT_SHUTDOWN_TIMEOUT;
	}
	if (gtmsource_options.statslog)
	{
		statslog_val_len = 4; /* max(strlen("ON"), strlen("OFF")) + 1 */
		if (!cli_get_str("STATSLOG", statslog_val, &statslog_val_len))
		{
			util_out_print("Error parsing STATSLOG qualifier", TRUE);
			return(-1);
		}
		UNIX_ONLY(cli_strupper(statslog_val);)
		if (0 == STRCMP(statslog_val, "ON"))
			gtmsource_options.statslog = TRUE;
		else if (0 == STRCMP(statslog_val, "OFF"))
			gtmsource_options.statslog = FALSE;
		else
		{
			util_out_print("Invalid value for STATSLOG qualifier, should be either ON or OFF", TRUE);
			return(-1);
		}
	}
	return(0);
}
