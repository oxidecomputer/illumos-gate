/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2023 Oxide Computer Company
 */

provider libipcc {
	probe msg(char *function, char *message);
};

#pragma D attributes Private/Private/Common provider libipcc provider
#pragma D attributes Private/Private/Common provider libipcc module
#pragma D attributes Private/Private/Common provider libipcc function
#pragma D attributes Private/Private/Common provider libipcc name
#pragma D attributes Private/Private/Common provider libipcc args

