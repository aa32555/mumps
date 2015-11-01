/****************************************************************
 *								*
 *	Copyright 2001, 2006 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"
#include <stdarg.h>
#include "gtm_string.h"
#include "cmd_qlf.h"
#include "compiler.h"
#include "cgp.h"
#include "io.h"
#include "list_file.h"
#include "gtmmsg.h"
#include "util_format.h"
#include "show_source_line.h"
#ifdef UNICODE_SUPPORTED
#include "gtm_utf8.h"
#endif

GBLREF char 			source_file_name[];
GBLREF unsigned char 		*source_buffer;
GBLREF short int 		source_name_len, last_source_column, source_line;
GBLREF int4 			source_error_found;
GBLREF command_qualifier	cmd_qlf;
GBLREF bool 			shift_gvrefs, run_time, dec_nofac;
GBLREF char			cg_phase;
GBLREF io_pair			io_curr_device, io_std_device;
#ifdef UNIX
GBLREF va_list			last_va_list_ptr;	/* set by util_format */
#endif

void stx_error(int in_error, ...)
{
	va_list	args;
	int	cnt, arg1, arg2, arg3, arg4;
	bool	list, warn;
	char	msgbuf[MAX_SRCLINE];
	char	buf[MAX_SRCLINE + LISTTAB + sizeof(ARROW)];
	char	*c;
	mstr	msg;

	error_def(ERR_SRCLIN);
	error_def(ERR_SRCLOC);
	error_def(ERR_SRCNAM);
	error_def(ERR_LABELMISSING);
	error_def(ERR_FMLLSTPRESENT);
	error_def(ERR_FMLLSTMISSING);
	error_def(ERR_ACTLSTTOOLONG);
	error_def(ERR_BADCHSET);
	error_def(ERR_BADCASECODE);
	error_def(ERR_INVDLRCVAL);
	error_def(ERR_CETOOMANY);
	error_def(ERR_CEUSRERROR);
	error_def(ERR_CEBIGSKIP);
	error_def(ERR_CETOOLONG);
	error_def(ERR_CENOINDIR);
	error_def(ERR_BADCHAR);

	flush_pio();
	va_start(args, in_error);
	shift_gvrefs = FALSE;
	if (run_time)
	{
		if (in_error == ERR_BADCHAR)
		{
			cnt = va_arg(args, int);
			assert(cnt == 4);
			arg1 = va_arg(args, int);
			arg2 = va_arg(args, int);
			arg3 = va_arg(args, int);
			arg4 = va_arg(args, int);
			va_end(args);
			rts_error(VARLSTCNT(6) in_error, cnt, arg1, arg2, arg3, arg4);
		} else if (in_error == ERR_LABELMISSING ||
		    in_error == ERR_FMLLSTPRESENT ||
		    in_error == ERR_FMLLSTMISSING ||
		    in_error == ERR_ACTLSTTOOLONG ||
		    in_error == ERR_BADCHSET ||
		    in_error == ERR_BADCASECODE
		    )
		{
			cnt = va_arg(args, int);
			assert(cnt == 2);
			arg1 = va_arg(args, int);
			arg2 = va_arg(args, int);
			va_end(args);
			rts_error(VARLSTCNT(4) in_error, cnt, arg1, arg2);
		} else if (in_error == ERR_CEUSRERROR ||
			in_error == ERR_INVDLRCVAL)
		{
			cnt = va_arg(args, int);
			assert(cnt == 1);
			arg1 = va_arg(args, int);
			va_end(args);
			rts_error(VARLSTCNT(3) in_error, cnt, arg1);
		} else
		{
			va_end(args);
			rts_error(VARLSTCNT(1) in_error);
		}
		GTMASSERT;
	} else if (cg_phase == CGP_PARSE)
		ins_errtriple(in_error);
	if (source_error_found)
	{
		va_end(args);
		return;
	}
	if (in_error != ERR_CETOOMANY &&	/* compiler escape errors shouldn't hide others */
	    in_error != ERR_CEUSRERROR &&
	    in_error != ERR_CEBIGSKIP &&
	    in_error != ERR_CETOOLONG &&
	    in_error != ERR_CENOINDIR)
	{
		source_error_found = (int4 ) in_error;
	}
	list = (cmd_qlf.qlf & CQ_LIST) != 0;
	warn = (cmd_qlf.qlf & CQ_WARNINGS) != 0;
	if (!warn && !list) /*SHOULD BE MESSAGE TYPE IS WARNING OR LESS*/
	{
		va_end(args);
		return;
	}

	if (list && io_curr_device.out == io_std_device.out)
		warn = FALSE;		/* if listing is going to $P, don't double output */

	if (in_error == ERR_BADCHAR)
	{
		memset(buf, ' ', LISTTAB);
		show_source_line(&buf[LISTTAB], warn);
		cnt = va_arg(args, int);
		assert(cnt == 4);
		arg1 = va_arg(args, int);
		arg2 = va_arg(args, int);
		arg3 = va_arg(args, int);
		arg4 = va_arg(args, int);
		if (warn)
		{
			dec_err(VARLSTCNT(6) in_error, 4, arg1, arg2, arg3, arg4);
			dec_err(VARLSTCNT(4) ERR_SRCNAM, 2, source_name_len, source_file_name);
		}
		if (list)
			list_line(buf);
		arg1 = arg2 = arg3 = arg4 = 0;
	} else if (in_error != ERR_LABELMISSING &&
	    in_error != ERR_FMLLSTPRESENT &&
	    in_error != ERR_FMLLSTMISSING &&
	    in_error != ERR_ACTLSTTOOLONG &&
	    in_error != ERR_BADCHSET &&
	    in_error != ERR_BADCASECODE)
	{
		memset(buf, ' ', LISTTAB);
		show_source_line(&buf[LISTTAB], warn);
		if (warn)
		{
			if ((in_error != ERR_CEUSRERROR) && (in_error != ERR_INVDLRCVAL))
				dec_err(VARLSTCNT(1) in_error);
			else
			{
				cnt = va_arg(args, int);
				assert(cnt == 1);
				arg1 = va_arg(args, int);
				dec_err(VARLSTCNT(3) in_error, 1, arg1);
			}
		}
		if (list)
			list_line(buf);
		arg1 = arg2 = 0;
	} else
	{
		cnt = va_arg(args, int);
		assert(cnt == 2);
		arg1 = va_arg(args, int);
		arg2 = va_arg(args, int);
		if (warn)
		{
			dec_err(VARLSTCNT(4) in_error, 2, arg1, arg2);
			dec_err(VARLSTCNT(4) ERR_SRCNAM, 2, source_name_len, source_file_name);
		}
	}
	va_end(args);
	if (list)
	{
		va_start(args, in_error);
		msg.addr = msgbuf;
		msg.len = sizeof(msgbuf);
		gtm_getmsg(in_error, &msg);
		assert(msg.len);
#ifdef UNIX
		c = util_format(msgbuf, args, LIT_AND_LEN(buf), MAXPOSINT4);
		va_end(last_va_list_ptr);	/* set by util_format */
#else
		c = util_format(msgbuf, args, LIT_AND_LEN(buf));
#endif
		va_end(args);
		*c = 0;
		list_line(buf);
	}
}
