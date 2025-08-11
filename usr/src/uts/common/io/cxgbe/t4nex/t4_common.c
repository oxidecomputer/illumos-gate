/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2025 Chelsio Communications, Inc.
 */
#include "t4_common.h"

const char * const t4_devlog_levels[] = {
	[FW_DEVLOG_LEVEL_EMERG]		= "EMERG",
	[FW_DEVLOG_LEVEL_CRIT]		= "CRIT",
	[FW_DEVLOG_LEVEL_ERR]		= "ERR",
	[FW_DEVLOG_LEVEL_NOTICE]	= "NOTICE",
	[FW_DEVLOG_LEVEL_INFO]		= "INFO",
	[FW_DEVLOG_LEVEL_DEBUG]		= "DEBUG"
};

const char * const t4_devlog_facilities[] = {
	[FW_DEVLOG_FACILITY_CORE]	= "CORE",
	[FW_DEVLOG_FACILITY_CF]		= "CF",
	[FW_DEVLOG_FACILITY_SCHED]	= "SCHED",
	[FW_DEVLOG_FACILITY_TIMER]	= "TIMER",
	[FW_DEVLOG_FACILITY_RES]	= "RES",
	[FW_DEVLOG_FACILITY_HW]		= "HW",
	[FW_DEVLOG_FACILITY_FLR]	= "FLR",
	[FW_DEVLOG_FACILITY_DMAQ]	= "DMAQ",
	[FW_DEVLOG_FACILITY_PHY]	= "PHY",
	[FW_DEVLOG_FACILITY_MAC]	= "MAC",
	[FW_DEVLOG_FACILITY_PORT]	= "PORT",
	[FW_DEVLOG_FACILITY_VI]		= "VI",
	[FW_DEVLOG_FACILITY_FILTER]	= "FILTER",
	[FW_DEVLOG_FACILITY_ACL]	= "ACL",
	[FW_DEVLOG_FACILITY_TM]		= "TM",
	[FW_DEVLOG_FACILITY_QFC]	= "QFC",
	[FW_DEVLOG_FACILITY_DCB]	= "DCB",
	[FW_DEVLOG_FACILITY_ETH]	= "ETH",
	[FW_DEVLOG_FACILITY_OFLD]	= "OFLD",
	[FW_DEVLOG_FACILITY_RI]		= "RI",
	[FW_DEVLOG_FACILITY_ISCSI]	= "ISCSI",
	[FW_DEVLOG_FACILITY_FCOE]	= "FCOE",
	[FW_DEVLOG_FACILITY_FOISCSI]	= "FOISCSI",
	[FW_DEVLOG_FACILITY_FOFCOE]	= "FOFCOE",
	[FW_DEVLOG_FACILITY_CHNET]	= "CHNET",
	[FW_DEVLOG_FACILITY_COISCSI]	= "COISCSI",
};

const char *
t4_devlog_level(uint8_t level)
{
	if (level < ARRAY_SIZE(t4_devlog_levels)) {
		return (t4_devlog_levels[level]);
	} else {
		return ("UNKNOWN");
	}
}

const char *
t4_devlog_facility(uint8_t facility)
{
	if (facility < ARRAY_SIZE(t4_devlog_facilities)) {
		return (t4_devlog_facilities[facility]);
	} else {
		return ("UNKNOWN");
	}
}

/*
 * Convert devlog entries and find the first entry based on timestamp.
 */
uint_t
t4_prep_devlog(struct fw_devlog_e *entries, uint_t nentries)
{
	uint_t first = 0;
	uint64_t ftstamp = UINT64_MAX;

	for (uint_t i = 0; i < nentries; i++) {
		struct fw_devlog_e *entry = &entries[i];

		if (entry->timestamp == 0)
			break;

		entry->timestamp = BE_64(entry->timestamp);
		entry->seqno = BE_32(entry->seqno);

		for (uint_t j = 0; j < 8; j++) {
			entry->params[j] = BE_32(entry->params[j]);
		}

		if (entry->timestamp < ftstamp) {
			ftstamp = entry->timestamp;
			first = i;
		}
	}

	return (first);
}
