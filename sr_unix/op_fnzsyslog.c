/****************************************************************
 *								*
 * Copyright (c) 2014-2022 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2020 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"

#include "gtm_multi_thread.h"
#include "min_max.h"
#include "op.h"
#include "util.h"

LITREF mval literal_one;

void op_fnzsyslog(mval* src, mval* dst)
{
	char		rebuff[OUT_BUFF_SIZE];
	int		len;
	char		*save_util_outptr;
	va_list		save_last_va_list_ptr;
	boolean_t	util_copy_saved = FALSE;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	MV_FORCE_STR(src);
	len = MIN(src->str.len, OUT_BUFF_SIZE - 1);
	if (0 < len)
	{
		memcpy(rebuff, src->str.addr, len);	/* Rebuffer to add null terminator */
		rebuff[len] = '\0';			/* Add null terminator */
<<<<<<< HEAD
		util_out_send_oper(rebuff, len);
=======
		util_out_print_args(NULL, 0, RESET);
		util_out_print_args(rebuff, 0, OPER);
		RESTORE_UTIL_OUT_BUFFER(save_util_outptr, save_last_va_list_ptr, util_copy_saved);
>>>>>>> eb3ea98c (GT.M V7.0-002)
	}
	memcpy(dst, &literal_one, SIZEOF(mval));
}
