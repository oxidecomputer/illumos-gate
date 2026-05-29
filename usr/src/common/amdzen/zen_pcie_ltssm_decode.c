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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * The mapping from the AMD/Zen 6-bit LC_STATE encoding to a name and a common
 * LTSSM (state, sub-state).
 *
 * This table has been validated for the Milan, Genoa and Turin
 * microarchitectures, which share the encoding (only the means of addressing
 * the registers differs). It is deliberately microarchitecture-specific: AMD
 * exposes a finer set of states than the PCIe specification's sub-states (for
 * example separate configuration "steps" and equalisation phases), and future
 * generations will add more (e.g. PCIe Gen6, expected in Venice, introduces the
 * L0p sub-state). The set of processor families known to use this encoding is
 * enforced by zen_ltssm_table_for() below. An unknown part will decode every
 * entry to PCIE_LTSSM_UNKNOWN.
 *
 * zl_name is AMD's own name for the state, which is sometimes more detailed.
 * zl_state and zl_substate give the common classification that consumers render
 * in the spec's dotted "State.SubState" form.
 */

#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/sysmacros.h>
#include <sys/pcie.h>
#include <sys/x86_archext.h>
#include <io/amdzen/zen_pcie_ltssm_decode.h>

typedef struct zen_ltssm_decode {
	const char		*zl_name;
	pcie_ltssm_state_t	zl_state;
	pcie_ltssm_substate_t	zl_substate;
} zen_ltssm_decode_t;

/*
 * Indexed by the raw 6-bit value. AMD's encoding distinguishes the receiver
 * and transmitter directions independently unlike the state in the spec.
 * Values not present here decode to PCIE_LTSSM_UNKNOWN.
 */
static const zen_ltssm_decode_t zen_ltssm_decode[] = {
	[0x00] = { "Detect_Quiet", PCIE_LTSSM_DETECT,
	    PCIE_LTSSM_SS_DETECT_QUIET },
	[0x01] = { "Start_Common_Mode", PCIE_LTSSM_DETECT,
	    PCIE_LTSSM_SS_DETECT_ACTIVE },
	[0x02] = { "Check_Common_Mode", PCIE_LTSSM_DETECT,
	    PCIE_LTSSM_SS_DETECT_ACTIVE },
	[0x03] = { "Rcvr_Detect", PCIE_LTSSM_DETECT,
	    PCIE_LTSSM_SS_DETECT_ACTIVE },
	[0x04] = { "No_Rcvr_Loop", PCIE_LTSSM_DETECT,
	    PCIE_LTSSM_SS_DETECT_ACTIVE },
	[0x05] = { "Poll_Quiet", PCIE_LTSSM_POLLING,
	    PCIE_LTSSM_SS_POLLING_ACTIVE },
	[0x06] = { "Poll_Active", PCIE_LTSSM_POLLING,
	    PCIE_LTSSM_SS_POLLING_ACTIVE },
	[0x07] = { "Poll_Compliance", PCIE_LTSSM_POLLING,
	    PCIE_LTSSM_SS_POLLING_COMPLIANCE },
	[0x08] = { "Poll_Config", PCIE_LTSSM_POLLING,
	    PCIE_LTSSM_SS_POLLING_CONFIGURATION },
	[0x09] = { "Config_Step1", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_LINKWIDTH_START },
	[0x0a] = { "Config_Step3", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_LANENUM_WAIT },
	[0x0b] = { "Config_Step5", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_COMPLETE },
	[0x0c] = { "Config_Step2", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_LINKWIDTH_ACCEPT },
	[0x0d] = { "Config_Step4", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_LANENUM_ACCEPT },
	[0x0e] = { "Config_Step6", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_COMPLETE },
	[0x0f] = { "Config_Idle", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_IDLE },
	[0x10] = { "Rcv_L0_Tx_L0", PCIE_LTSSM_L0, PCIE_LTSSM_SS_NONE },
	[0x11] = { "Rcv_L0_Tx_L0_Idle", PCIE_LTSSM_L0,
	    PCIE_LTSSM_SS_NONE },
	[0x12] = { "Rcv_L0_Tx_L0s", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_ENTRY },
	[0x13] = { "Rcv_L0_Tx_L0s_FTS", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_FTS },
	[0x14] = { "Rcv_L0s_Tx_L0", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_IDLE },
	[0x15] = { "Rcv_L0s_Tx_L0_Idle", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_IDLE },
	[0x16] = { "Rcv_L0s_Tx_L0s", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_ENTRY },
	[0x17] = { "Rcv_L0s_Tx_L0s_FTS", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_FTS },
	[0x18] = { "L1_Entry", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_ENTRY },
	[0x19] = { "L1_Idle", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_IDLE },
	[0x1a] = { "L1_Wait", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_ENTRY },
	[0x1b] = { "L1", PCIE_LTSSM_L1, PCIE_LTSSM_SS_NONE },
	[0x1c] = { "L23_Stall", PCIE_LTSSM_L2, PCIE_LTSSM_SS_L2_IDLE },
	[0x1d] = { "L23_Entry", PCIE_LTSSM_L2, PCIE_LTSSM_SS_L2_IDLE },
	[0x1e] = { "L23_Idle", PCIE_LTSSM_L2, PCIE_LTSSM_SS_L2_IDLE },
	[0x1f] = { "L23_Ready", PCIE_LTSSM_L2, PCIE_LTSSM_SS_L2_IDLE },
	[0x20] = { "Recovery_Lock", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_RCVRLOCK },
	[0x21] = { "Recovery_Config", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_RCVRCFG },
	[0x22] = { "Recovery_Idle", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_IDLE },
	[0x23] = { "Training_Bit", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_RCVRLOCK },
	[0x24] = { "Rcvd_Loopback", PCIE_LTSSM_LOOPBACK,
	    PCIE_LTSSM_SS_LOOPBACK_ACTIVE },
	[0x25] = { "Rcvd_Loopback_Idle", PCIE_LTSSM_LOOPBACK,
	    PCIE_LTSSM_SS_LOOPBACK_EXIT },
	/*
	 * Inferred from AMD's naming and the spec: Hot Reset is reached from
	 * Recovery.Idle on receipt of TS1s with the hot-reset bit set, which we
	 * take Rcvd_Reset_Idle to represent. AMD's exact semantics for this
	 * encoding are not documented.
	 */
	[0x26] = { "Rcvd_Reset_Idle", PCIE_LTSSM_HOT_RESET,
	    PCIE_LTSSM_SS_NONE },
	[0x27] = { "Rcvd_Disable_Entry", PCIE_LTSSM_DISABLED,
	    PCIE_LTSSM_SS_NONE },
	[0x28] = { "Rcvd_Disable_Idle", PCIE_LTSSM_DISABLED,
	    PCIE_LTSSM_SS_NONE },
	[0x29] = { "Rcvd_Disable", PCIE_LTSSM_DISABLED,
	    PCIE_LTSSM_SS_NONE },
	[0x2a] = { "Detect_Idle", PCIE_LTSSM_DETECT,
	    PCIE_LTSSM_SS_DETECT_QUIET },
	[0x2b] = { "L23_Wait", PCIE_LTSSM_L2, PCIE_LTSSM_SS_L2_IDLE },
	[0x2c] = { "Rcv_L0s_Skp_Tx_L0", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_IDLE },
	[0x2d] = { "Rcv_L0s_Skp_Tx_L0_Idle", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_IDLE },
	[0x2e] = { "Rcv_L0s_Skp_Tx_L0s", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_ENTRY },
	[0x2f] = { "Rcv_L0s_Skp_Tx_L0s_FTS", PCIE_LTSSM_L0S,
	    PCIE_LTSSM_SS_L0S_FTS },
	[0x30] = { "Config_Step2b", PCIE_LTSSM_CONFIG,
	    PCIE_LTSSM_SS_CONFIG_LINKWIDTH_ACCEPT },
	[0x31] = { "Recovery_Speed", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_SPEED },
	[0x32] = { "Poll_Compliance_Idle", PCIE_LTSSM_POLLING,
	    PCIE_LTSSM_SS_POLLING_COMPLIANCE },
	[0x33] = { "Rcvd_Loopback_Speed", PCIE_LTSSM_LOOPBACK,
	    PCIE_LTSSM_SS_LOOPBACK_ACTIVE },
	[0x34] = { "Recovery_Eq0", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_EQUALIZATION },
	[0x35] = { "Recovery_Eq1", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_EQUALIZATION },
	[0x36] = { "Recovery_Eq2", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_EQUALIZATION },
	[0x37] = { "Recovery_Eq3", PCIE_LTSSM_RECOVERY,
	    PCIE_LTSSM_SS_RECOVERY_EQUALIZATION },
	[0x38] = { "L1_1", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_1 },
	[0x39] = { "L1_2_Entry", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_2 },
	[0x3a] = { "L1_2_Idle", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_2 },
	[0x3b] = { "L1_2_Exit", PCIE_LTSSM_L1, PCIE_LTSSM_SS_L1_2 },
};

/*
 * Return the decode table for a given AMD processor family, or NULL if we have
 * no validated table for it. Milan, Genoa and Turin (classic and dense) share
 * the encoding above; any other family (including a future Gen6 part such as
 * Venice, whose L0p sub-states are not represented here) must be validated and
 * added here before its LC_STATE values can be decoded.
 */
static const zen_ltssm_decode_t *
zen_ltssm_table_for(x86_processor_family_t fam, size_t *nentsp)
{
	switch (fam) {
	case X86_PF_AMD_MILAN:
	case X86_PF_AMD_GENOA:
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		*nentsp = ARRAY_SIZE(zen_ltssm_decode);
		return (zen_ltssm_decode);
	default:
		return (NULL);
	}
}

bool
zen_ltssm_lookup(x86_processor_family_t fam, uint8_t raw, const char **namep,
    pcie_ltssm_state_t *statep, pcie_ltssm_substate_t *substatep)
{
	const zen_ltssm_decode_t *table;
	size_t nents;

	table = zen_ltssm_table_for(fam, &nents);
	if (table == NULL || raw >= nents || table[raw].zl_name == NULL)
		return (false);

	if (namep != NULL)
		*namep = table[raw].zl_name;
	if (statep != NULL)
		*statep = table[raw].zl_state;
	if (substatep != NULL)
		*substatep = table[raw].zl_substate;
	return (true);
}
