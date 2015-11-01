/****************************************************************
 *								*
 *	Copyright 2004, 2006 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"
#include "gtm_ctype.h"

#include "gtmimagename.h"
#include "gtm_logicals.h"
#include "trans_numeric.h"
#include "trans_log_name.h"
#include "logical_truth_value.h"
#include "iosp.h"		/* for SS_ */
#include "nametabtyp.h"		/* for namelook */
#include "namelook.h"
#include "io.h"
#include "iottdef.h"
#include "gtm_env_init.h"	/* for gtm_env_init() and gtm_env_init_sp() prototype */
#include "gtm_utf8.h"		/* UTF8_NAME */

GBLREF	int4			gtm_shmflags;	/* Shared memory flags for shmat() */
GBLREF	uint4			gtm_principal_editing_defaults;	/* ext_cap flags if tt */
GBLREF	boolean_t		gtm_utf8_mode;
GBLREF	boolean_t		utf8_patnumeric;
GBLREF	boolean_t		badchar_inhibit;
GBLREF	boolean_t		gtm_quiet_halt;

static nametabent editing_params[] =
{
	{7, "EDITING"},
	{6, "INSERT"},
	{9, "NOEDITING"},
	{8, "NOINSERT"}
};

static unsigned char editing_index[27] =
{
	0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
	2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4
};

/* Unix only environment initializations */
void	gtm_env_init_sp(void)
{
	mstr		val, trans;
	int4		status, index;
	boolean_t	ret, is_defined;
	char		buf[MAX_TRANS_NAME_LEN], *token;

	val.addr = GTM_SHMFLAGS;
	val.len = sizeof(GTM_SHMFLAGS) - 1;
	gtm_shmflags = (int4)trans_numeric(&val, &is_defined, TRUE);	/* Flags vlaue (0 is undefined or bad) */

	val.addr = GTM_QUIET_HALT;
	val.len = sizeof(GTM_QUIET_HALT) - 1;
	ret = logical_truth_value(&val, FALSE, &is_defined);
	if (is_defined)
		gtm_quiet_halt = ret;

	gtm_principal_editing_defaults = 0;
	val.addr = GTM_PRINCIPAL_EDITING;
	val.len = sizeof(GTM_PRINCIPAL_EDITING) - 1;
	if (SS_NORMAL == (status = trans_log_name(&val, &trans, buf)))
	{
		assert(trans.len < sizeof(buf));
		trans.addr[trans.len] = '\0';
		token = strtok(trans.addr, ":");
		while (NULL != token)
		{
			if (ISALPHA(token[0]))
				index = namelook(editing_index, editing_params, STR_AND_LEN(token));
			else
				index = -1;	/* ignore this token */
			if (0 <= index)
			{
				switch (index)
				{
				case 0:	/* EDITING */
					gtm_principal_editing_defaults |= TT_EDITING;
					break;
				case 1:	/* INSERT */
					gtm_principal_editing_defaults &= ~TT_NOINSERT;
					break;
				case 2:	/* NOEDITING */
					gtm_principal_editing_defaults &= ~TT_EDITING;
					break;
				case 3:	/* NOINSERT */
					gtm_principal_editing_defaults |= TT_NOINSERT;
					break;
				}
			}
			token = strtok(NULL, ":");
		}
	}

	val.addr = GTM_CHSET_ENV;
	val.len = STR_LIT_LEN(GTM_CHSET_ENV);
	if (SS_NORMAL == (status = trans_log_name(&val, &trans, buf)) && STR_LIT_LEN(UTF8_NAME) == trans.len)
	{
		if (!strncasecmp(buf, UTF8_NAME, STR_LIT_LEN(UTF8_NAME)))
		{
			gtm_utf8_mode = TRUE;
			/* Initialize $ZPATNUMERIC only if $ZCHSET is "UTF-8" */
			val.addr = GTM_PATNUMERIC_ENV;
			val.len = STR_LIT_LEN(GTM_PATNUMERIC_ENV);
			if (SS_NORMAL == (status = trans_log_name(&val, &trans, buf)) &&
				STR_LIT_LEN(UTF8_NAME) == trans.len &&
				!strncasecmp(buf, UTF8_NAME, STR_LIT_LEN(UTF8_NAME)))
			{
					utf8_patnumeric = TRUE;
			}

			val.addr = GTM_BADCHAR_ENV;
			val.len = STR_LIT_LEN(GTM_BADCHAR_ENV);
			status = logical_truth_value(&val, TRUE, &is_defined);
			if (is_defined)
				badchar_inhibit = status ? TRUE : FALSE;
		}
	}
}
