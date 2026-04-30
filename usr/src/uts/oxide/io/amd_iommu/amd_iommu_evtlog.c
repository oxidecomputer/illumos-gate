/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2026 Oxide Computer Company
 */

#include <sys/stdbool.h>
#include <sys/sysmacros.h>
#include <sys/stdint.h>
#include <sys/sunddi.h>
#include <sys/amd_iommu.h>

#include "amd_iommu_impl.h"

/*
 * Functions and data used when interacting with the IOMMU's event log.
 *
 * Currently, we merely poll event logs in response to commands that manipulate
 * the IOMMU, such as allocating, freeing, or updating an an IRTE or DTE.
 * However, we really ought to make the IOMMU itself generate an interrupt when
 * it emits a log entry: this is future work we plan to take on once we have a
 * device taxonomy that more closely resembles the fabric as defined by the
 * hardware.
 *
 * Concomitant with that, we would like to plumb exceptional events into the
 * fault management framework: right now, we merely print them to the console,
 * where they are unlikely to be noticed.
 *
 * At anyrate, presently we focus on logging information relevant to our use of
 * the IOMMU, which means we prioritize interrupt remapping, and give only
 * passing treatment to address translations, PASIDs, and vIOMMU events.
 *
 * - Since vIOMMU is disabled, we never log VCmd, Vppr, VEvent, or VNR.
 * - We don't use PASID so never log it.
 * - We decode IOPF events, but don't use DomainIDs in DTEs, so don't log D/P;
 *   we do print GN to show clearly that it is disabled.
 * - Fields that would describe address translation events are not emitted:
 *   no RW, NX, U/S, or PE.
 */

/*
 * The number of times to spin waiting for the event log running bit to clear
 * in the IOMMU status register.
 *
 * Note that AMD 48882 says to spin on the `EventLogRun` status bit, "so that
 * all log entries are completed as circumstances allow.  EventLogRun must be 0b
 * to modify the event log registers safely."  Unfortunately, it does not say
 * how long to spin; we arbitrarily chose 2^16 tries, but this is tunable.  If
 * we time out, we try to restart event logging anyway.
 */
static uint_t amd_iommu_evtlog_restart_max_spin = 1U << 16;

static const char *event_types[] = {
	[AMD_IOMMU_EVT_RESERVED] = "Reserved",
	[AMD_IOMMU_EVT_ILLEGAL_DEV_TABLE_ENTRY] = "Illegal Device Table Entry",
	[AMD_IOMMU_EVT_IO_PAGE_FAULT] = "IO Page Fault",
	[AMD_IOMMU_EVT_DEV_TAB_HARDWARE_ERROR] = "Device Table Hardware Error",
	[AMD_IOMMU_EVT_PAGE_TAB_HARDWARE_ERROR] = "Page Table Hardware Error",
	[AMD_IOMMU_EVT_ILLEGAL_COMMAND_ERROR] = "Illegal Command Error",
	[AMD_IOMMU_EVT_COMMAND_HARDWARE_ERROR] = "Command Hardware Error",
	[AMD_IOMMU_EVT_IOTLB_INVAL_TIMEOUT] = "IOTLB Invalidation Timeout",
	[AMD_IOMMU_EVT_INVALID_DEVICE_REQUEST] = "Invalid Device Request",
	[AMD_IOMMU_EVT_INVALID_PPR_REQUEST] = "Invalid PPR Request",
	[AMD_IOMMU_EVT_EVENT_COUNTER_ZERO] = "Event Counter Zero",
};

static void
log_devtab_illegal_entry(amd_iommu_t *iommu, amd_iommu_evle_idte_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT3U(event->aie_idte_evt_code, ==,
	    AMD_IOMMU_EVT_ILLEGAL_DEV_TABLE_ENTRY);

	uint16_t device_id = event->aie_idte_device_id;
	uint64_t addr = event->aie_idte_address_hi;
	uint64_t addr_lo = event->aie_idte_address_lo;
	addr = (addr << 32) | (addr_lo << 2);
	const char *evt = event_types[AMD_IOMMU_EVT_ILLEGAL_DEV_TABLE_ENTRY];
	cmn_err(CE_WARN, "amd_iommu%u: %s: device id = %hu, "
	    "address = %p, GN/I/TR/RZ = [%u,%u,%u,%u].",
	    iommu->ai_instance, evt, device_id, (void *)addr,
	    event->aie_idte_gn, event->aie_idte_i, event->aie_idte_tr,
	    event->aie_idte_rz);
}

static void
log_io_page_fault(amd_iommu_t *iommu, amd_iommu_evle_iopf_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT3U(event->aie_iopf_evt_code, ==, AMD_IOMMU_EVT_IO_PAGE_FAULT);

	const char *evt = event_types[AMD_IOMMU_EVT_IO_PAGE_FAULT];
	uint16_t device_id = event->aie_iopf_device_id;
	uint64_t addr = event->aie_iopf_address_hi;
	addr = (addr << 32) | (uint64_t)event->aie_iopf_address_lo;

	cmn_err(CE_WARN, "amd_iommu%u: %s: device id = %hu, "
	    "address = %p, GN/I/TR/PR/RZ = [%u,%u,%u,%u,%u].",
	    iommu->ai_instance, evt, device_id, (void *)addr,
	    event->aie_iopf_gn, event->aie_iopf_i, event->aie_iopf_tr,
	    event->aie_iopf_pr, event->aie_iopf_rz);
}

static const char *hardware_errs[] = {
	"Invalid COMPLETION_WAIT store address while SNP[en]",
	"Master Abort", "Target Abort", "Data Error"
};

static const char *
hardware_err_type(uint8_t type)
{

	if (type >= ARRAY_SIZE(hardware_errs))
		return ("Unknown");

	return (hardware_errs[type]);
}

static void
log_devtab_hw_error(amd_iommu_t *iommu, amd_iommu_evle_dthe_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT3U(event->aie_dthe_evt_code, ==,
	    AMD_IOMMU_EVT_DEV_TAB_HARDWARE_ERROR);

	const char *evt = event_types[AMD_IOMMU_EVT_DEV_TAB_HARDWARE_ERROR];
	uint16_t device_id = event->aie_dthe_device_id;
	uint_t err_type = event->aie_dthe_type;
	const char *err = hardware_err_type(err_type);
	uint64_t phys_addr = event->aie_dthe_address_hi;
	uint64_t phys_addr_lo = event->aie_dthe_address_lo;
	phys_addr = (phys_addr << 32) | (phys_addr_lo << 4);
	cmn_err(CE_WARN,
	    "amd_iommu%u: %s: device id = %hu, hardware error type = %s, "
	    "physical address = %p.",
	    iommu->ai_instance, evt, device_id, err, (void *)phys_addr);
}

static void
log_illegal_cmd(amd_iommu_t *iommu, amd_iommu_evle_ice_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT3U(event->aie_ice_evt_code, ==,
	    AMD_IOMMU_EVT_ILLEGAL_COMMAND_ERROR);

	const char *evt = event_types[AMD_IOMMU_EVT_ILLEGAL_COMMAND_ERROR];
	uint64_t phys_addr = event->aie_ice_address_hi;
	uint64_t phys_addr_lo = event->aie_ice_address_lo;
	phys_addr = (phys_addr << 32) | (phys_addr_lo << 4);
	cmn_err(CE_WARN, "amd_iommu%u: %s: physical address = %p.",
	    iommu->ai_instance, evt, (void *)phys_addr);
}

static void
log_command_hw_error(amd_iommu_t *iommu, amd_iommu_evle_che_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT3U(event->aie_che_evt_code, ==,
	    AMD_IOMMU_EVT_COMMAND_HARDWARE_ERROR);

	const char *evt = event_types[AMD_IOMMU_EVT_COMMAND_HARDWARE_ERROR];
	uint_t err_type = event->aie_che_type;
	const char *err = hardware_err_type(err_type);
	uint64_t phys_addr = event->aie_che_address_hi;
	uint64_t phys_addr_lo = event->aie_che_address_lo;
	phys_addr = (phys_addr << 32) | (phys_addr_lo << 4);
	cmn_err(CE_WARN, "amd_iommu%u: %s: hardware error type = %s, "
	    "physical address = %p.", iommu->ai_instance, evt, err,
	    (void *)phys_addr);
}

static const char *invalid_req_errs[] = {
	"Read or Non-posted Write in INTR Range",
	"Pre-translated transaction from device with I=0 or V=0",
	"Port-IO transaction for device with IoCtl = 00b",

	"Posted write to SysMgt with device SysMgt=00b "
	"OR SysMgt=10b && message not INTx "
	"OR Posted write to addr translation range with HtAtsResv=1",

	"Read request or non-posted write in SysMgt with "
	"device SysMgt=10b or 0xb OR Read request or non-posted write "
	"in addr translation range with HtAtsResv=1",

	"Posted write to Interrupt/EOI Range for device with IntCtl=00b",
	"Posted write to reserved Interrupt Address Range",

	"Transaction to SysMgt when SysMgt=11b OR "
	"transaction to Port-IO when IoCtl=10b while V=1 TV=0",
};

static const char *invalid_req_errs_tr[] = {
	"Translation I=0/V=0/V=1&&TV=0",

	"Translation INTR/Port-IO/SysMgt OR "
	"translation when SysMgt=11b/Port-IO and IOCTL=10b and V=1 && TV=0",

	"When PASID supported translation request for address with TLP "
	"prefix when guest translation inactive or unsupported for IO "
	"device (GN=1, PASID valid)",
};

static const char *
invalid_req_type(uint8_t type, bool tr)
{
	if (tr) {
		if (type >= ARRAY_SIZE(invalid_req_errs_tr))
			return ("Reserved");
		return (invalid_req_errs_tr[type]);
	} else if (type >= ARRAY_SIZE(invalid_req_errs)) {
		return ("Unknown error");
	}

	return (invalid_req_errs[type]);
}

static void
log_invalid_device_req(amd_iommu_t *iommu, amd_iommu_evle_idr_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT3U(event->aie_idr_evt_code, ==,
	    AMD_IOMMU_EVT_INVALID_DEVICE_REQUEST);

	const char *evt = event_types[AMD_IOMMU_EVT_INVALID_DEVICE_REQUEST];
	uint16_t device_id = event->aie_idr_device_id;
	uint64_t addr = event->aie_idr_address_hi;
	addr = (addr << 32) | (uint64_t)event->aie_idr_address_lo;
	const char *err =
	    invalid_req_type(event->aie_idr_type, event->aie_idr_tr);
	cmn_err(CE_WARN, "amd_iommu%u: %s: device id = %hu, %s request, "
	    "error = %s, address = %p.", iommu->ai_instance, evt,
	    device_id, event->aie_idr_tr == 1 ? "Translation" : "Transaction",
	    err, (void *)addr);
}

static void
log_unexpected_event(amd_iommu_t *iommu, amd_iommu_evle_t *event)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);

	const char *evt = "Unknown";
	if (event->aievle_evt_code < ARRAY_SIZE(event_types))
		evt = event_types[event->aievle_evt_code];
	cmn_err(CE_WARN, "amd_iommu%u: Ignoring unexpected event: %s.",
	    iommu->ai_instance, evt);
}

static bool
read_event(amd_iommu_event_log_t *evtlog, amd_iommu_evle_t *event, size_t tail)
{
	ASSERT3P(evtlog, !=, NULL);
	ASSERT3P(evtlog->aiel_iommu, !=, NULL);
	ASSERT3P(event, !=, NULL);
	ASSERT(MUTEX_HELD(&evtlog->aiel_lock));

	size_t head = evtlog->aiel_head;
	if (head == tail)
		return (false);

	*event = evtlog->aiel_evt_log[head];
	head++;
	if (head >= AMD_IOMMU_EVENT_LOG_NENTS)
		head = 0;
	amd_iommu_write_evtlog_head(evtlog, head);
	evtlog->aiel_head = head;

	return (true);
}

static bool
process_one_event_log_entry(amd_iommu_event_log_t *evtlog, size_t tail)
{
	ASSERT3P(evtlog, !=, NULL);
	ASSERT3P(evtlog->aiel_iommu, !=, NULL);
	ASSERT(MUTEX_HELD(&evtlog->aiel_lock));

	amd_iommu_t *iommu = evtlog->aiel_iommu;
	amd_iommu_evle_t event;

	if (!read_event(evtlog, &event, tail))
		return (false);

	switch (event.aievle_evt_code) {
	case AMD_IOMMU_EVT_ILLEGAL_DEV_TABLE_ENTRY:
		log_devtab_illegal_entry(iommu,
		    (amd_iommu_evle_idte_t *)&event);
		break;
	case AMD_IOMMU_EVT_IO_PAGE_FAULT:
		log_io_page_fault(iommu,
		    (amd_iommu_evle_iopf_t *)&event);
		break;
	case AMD_IOMMU_EVT_DEV_TAB_HARDWARE_ERROR:
		log_devtab_hw_error(iommu,
		    (amd_iommu_evle_dthe_t *)&event);
		break;
	case AMD_IOMMU_EVT_ILLEGAL_COMMAND_ERROR:
		log_illegal_cmd(iommu, (amd_iommu_evle_ice_t *)&event);
		break;
	case AMD_IOMMU_EVT_COMMAND_HARDWARE_ERROR:
		log_command_hw_error(iommu,
		    (amd_iommu_evle_che_t *)&event);
		break;
	case AMD_IOMMU_EVT_INVALID_DEVICE_REQUEST:
		log_invalid_device_req(iommu,
		    (amd_iommu_evle_idr_t *)&event);
		break;
	default:
		log_unexpected_event(iommu, &event);
		break;
	}

	return (true);
}

static bool
evtlog_overflowed(amd_iommu_event_log_t *evtlog)
{
	ASSERT3P(evtlog, !=, NULL);
	amd_iommu_t *iommu = evtlog->aiel_iommu;
	ASSERT3P(iommu, !=, NULL);
	amd_iommu_mmio_reg_status_t status;
	status = amd_iommu_read_status(iommu);
	return (status.aimr_status_evt_overflow == 1);
}

static bool
evtlog_is_running(amd_iommu_event_log_t *evtlog)
{
	ASSERT3P(evtlog, !=, NULL);
	amd_iommu_t *iommu = evtlog->aiel_iommu;
	ASSERT3P(iommu, !=, NULL);
	amd_iommu_mmio_reg_status_t status;
	status = amd_iommu_read_status(iommu);
	return (status.aimr_status_evt_log_run == 1);
}

static void
iommu_evtlog_set_en(amd_iommu_t *iommu, bool en)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT(MUTEX_HELD(&iommu->ai_ctl_lock));

	amd_iommu_mmio_reg_ctl_t ctl = amd_iommu_read_ctl(iommu);
	ctl.aimr_ctl_evt_log_en = en ? 1 : 0;
	amd_iommu_write_ctl(iommu, ctl);
}

/*
 * Flushes the event log.  We will process up to a maximum number of entries,
 * and will discard the rest if that number is exceeded.  Realistically, the
 * only way the event log fills is if something is spamming it with repeated
 * entries.  While unlikely in practice, there's little utility in logging the
 * same thing thousands of times, beyond knowing that the IOMMU is very unhappy.
 */
static void
iommu_flush_event_log(amd_iommu_t *iommu)
{
	ASSERT3P(iommu, !=, NULL);
	amd_iommu_event_log_t *evtlog = iommu->ai_evt_log;
	ASSERT3P(evtlog, !=, NULL);
	ASSERT(MUTEX_HELD(&evtlog->aiel_lock));

	size_t tail = amd_iommu_read_evtlog_tail(evtlog);
	for (size_t k = 0; k < AMD_IOMMU_EVENT_LOG_MAX_READ; k++) {
		if (!process_one_event_log_entry(evtlog, tail))
			break;
	}
	if (evtlog->aiel_head == tail)
		return;
	/* Flush the rest */
	size_t discarded =
	    (size_t)(tail - evtlog->aiel_head) % AMD_IOMMU_EVENT_LOG_NENTS;
	cmn_err(CE_WARN, "amd_iommu%u: discarded %zu event log entries",
	    iommu->ai_instance, discarded);
	amd_iommu_write_evtlog_head(evtlog, tail);
	evtlog->aiel_head = tail;
}

/*
 * Restart the event log, if it has overflowed.  If it has not overflowed, this
 * is a nop.
 */
static void
restart_evtlog_if_overflowed(amd_iommu_event_log_t *evtlog)
{
	ASSERT3P(evtlog, !=, NULL);
	amd_iommu_t *iommu = evtlog->aiel_iommu;
	ASSERT3P(iommu, !=, NULL);
	ASSERT(MUTEX_HELD(&evtlog->aiel_lock));

	if (!evtlog_overflowed(evtlog))
		return;
	cmn_err(CE_WARN, "amd_iommu%u: Event log overflowed; restarting.",
	    iommu->ai_instance);
	for (uint_t spin = 0; spin < amd_iommu_evtlog_restart_max_spin; spin++)
		if (!evtlog_is_running(evtlog))
			break;
	mutex_enter(&iommu->ai_ctl_lock);
	iommu_evtlog_set_en(iommu, false);
	amd_iommu_mmio_reg_status_t status = { 0 };
	iommu_flush_event_log(iommu);
	status.aimr_status_evt_overflow = 1;
	status.aimr_status_evt_log_int = 1;
	amd_iommu_write_status(iommu, status);
	iommu_evtlog_set_en(iommu, true);
	mutex_exit(&iommu->ai_ctl_lock);
}

/*
 * Eventually, we will likely want to invoke this in response to an interrupt.
 */
int
amd_iommu_process_event_log(amd_iommu_t *iommu)
{
	amd_iommu_event_log_t *evtlog;

	VERIFY3P(iommu, !=, NULL);
	evtlog = iommu->ai_evt_log;
	VERIFY3P(evtlog, !=, NULL);
	mutex_enter(&evtlog->aiel_lock);
	restart_evtlog_if_overflowed(evtlog);
	size_t tail = amd_iommu_read_evtlog_tail(evtlog);
	for (size_t k = 0; k < AMD_IOMMU_EVENT_LOG_MAX_READ; k++) {
		if (!process_one_event_log_entry(evtlog, tail))
			break;
	}
	mutex_exit(&evtlog->aiel_lock);

	return (DDI_SUCCESS);
}

int
amd_iommu_process_segment_event_logs(amd_iommu_segment_t *segment)
{
	VERIFY3P(segment, !=, NULL);
	for (amd_iommu_t *iommu = list_head(&segment->ais_iommus);
	    iommu != NULL; iommu = list_next(&segment->ais_iommus, iommu)) {
		if (amd_iommu_process_event_log(iommu) != DDI_SUCCESS)
			return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}
