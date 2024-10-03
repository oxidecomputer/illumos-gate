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
 * Copyright 2024 Oxide Computer Company
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/boot_debug.h>
#include <sys/kernel_ipcc.h>
#include <sys/platform_detect.h>
#include <sys/param.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/archsystm.h>
#include <sys/x86_archext.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/fch/misc.h>
#include <sys/amdzen/fch.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/milan/iomux.h>
#include <sys/io/genoa/iomux.h>
#include <sys/io/turin/iomux.h>
#include <sys/io/zen/platform.h>


extern x86_chiprev_t _cpuid_chiprev(uint_t, uint_t, uint_t, uint_t);
extern const char *_cpuid_chiprevstr(uint_t, uint_t, uint_t, uint_t);
extern x86_uarchrev_t _cpuid_uarchrev(uint_t, uint_t, uint_t, uint_t);
extern uint32_t _cpuid_skt(uint_t, uint_t, uint_t, uint_t);

const oxide_board_data_t *oxide_board_data = NULL;

struct oxide_board_test;
typedef struct oxide_board_test oxide_board_test_t;
struct oxide_board_testresult;
typedef struct oxide_board_testresult oxide_board_testresult_t;

typedef struct oxide_board_iomux {
	const uint32_t		obp_gpio;
	const uint32_t		obp_iomux;
	const bool		obp_valid;
} oxide_board_iomux_t;

#define	IOMUX_CFG_ENTRY(g, m) { \
	.obp_gpio = (g), \
	.obp_iomux = (m), \
	.obp_valid = true }

/* GPIO tristate tests */

typedef enum {
	OGS_DONTCARE = 0,
	OGS_HIGH,
	OGS_LOW,
} oxide_gpio_state_t;

typedef struct oxide_gpio_tristate {
	oxide_gpio_state_t	ogt_floating;
	oxide_gpio_state_t	ogt_pulledup;
	oxide_gpio_state_t	ogt_pulleddown;
} oxide_gpio_tristate_t;

typedef void (*oxide_gpio_tristate_init_f)(const oxide_board_cpuinfo_t *,
    const oxide_board_test_t *);
typedef void (*oxide_gpio_tristate_fini_f)(const oxide_board_cpuinfo_t *,
    const oxide_board_test_t *);

typedef struct oxide_test_gpio_tristate {
	const uint32_t				otgt_gpionum;
	const oxide_board_iomux_t		otgt_iomux;
	const oxide_gpio_tristate_init_f	otgt_init;
	const oxide_gpio_tristate_fini_f	otgt_fini;
	const oxide_gpio_tristate_t		otgt_expect;
} oxide_test_gpio_tristate_t;

/* Oxide board tests */

/*
 * Array sizes to accommodate current board definitions. Increase as necessary
 * when adding new entries.
 */
#define	OXIDE_BOARD_CHIPREVS	3
#define	OXIDE_BOARD_TESTS	3
#define	OXIDE_BOARD_IOMUX	10

typedef bool (*oxide_board_test_f)(const oxide_board_cpuinfo_t *cpuinfo,
    const oxide_board_test_t *, oxide_board_testresult_t *);

typedef struct oxide_board_test {
	const oxide_board_test_f	obt_func;
	union {
		const uint32_t		obt_socket;
		const x86_chiprev_t	obt_chiprev[OXIDE_BOARD_CHIPREVS];
		const oxide_test_gpio_tristate_t	obt_tristate;
		const uint32_t		obt_romtype;
	};
} oxide_board_test_t;

typedef struct oxide_board_testresult {
	union {
		oxide_gpio_tristate_t	obtr_tristate;
		uint32_t		obtr_romtype;
	};
} oxide_board_testresult_t;

/* Oxide board definitions */

typedef struct oxide_board_def {
	oxide_board_data_t		obdef_board_data;
	const oxide_board_iomux_t	obdef_iomux[OXIDE_BOARD_IOMUX];
	const oxide_board_test_t	obdef_tests[OXIDE_BOARD_TESTS];
	oxide_board_testresult_t	obdef_testresults[OXIDE_BOARD_TESTS];
} oxide_board_def_t;

static bool eb_eval_chiprev(const oxide_board_cpuinfo_t *, const
    oxide_board_test_t *, oxide_board_testresult_t *);
static bool eb_eval_socket(const oxide_board_cpuinfo_t *, const
    oxide_board_test_t *, oxide_board_testresult_t *);
static bool eb_eval_gpio_tristate(const oxide_board_cpuinfo_t *, const
    oxide_board_test_t *, oxide_board_testresult_t *);
static bool eb_eval_romtype(const oxide_board_cpuinfo_t *, const
    oxide_board_test_t *, oxide_board_testresult_t *);

static void eb_disable_kbrst(const oxide_board_cpuinfo_t *,
    const oxide_board_test_t *);
static void eb_enable_kbrst(const oxide_board_cpuinfo_t *,
    const oxide_board_test_t *);

/*
 * This is a table of boards that may be present in an Oxide system, followed
 * by a generic default entry that will be selected if the board cannot be
 * identified to facilitate bring up of new platforms.
 *
 * The table is searched in order and the first entry for which all of the
 * tests defined in .obdef_tests are successful is selected and assigned to
 * the global `oxide_board_data`. This structure is then filled in with
 * additional data that can be detected or derived from the running system,
 * such as the socket type and the Fusion Controller Hub [FCH] kind.
 *
 * After a board is identified, the associated iomux settings in .obdef_iomux
 * are applied in order. The settings applied here should only be those which
 * are necessary for the system console and IPCC to operate.
 *
 * When specifying chiprev values for the `.obt_chiprev` tests, note that
 * it is possible to combine multiple revisions in a single entry, but that
 * does not work for different families or models -- those must be listed
 * separately. For example it is possible specify a single entry that will
 * match both Milan B0 and B1, but not one that matches both Milan and
 * Genoa.
 */
static oxide_board_def_t oxide_board_defs[] = {
	{
		.obdef_board_data = {
			.obd_board = OXIDE_BOARD_GIMLET,
			.obd_rootnexus = "Oxide,Gimlet",
			.obd_bsu_slot = { 17, 18 },
			.obd_ipccmode = IPCC_MODE_UART1,
			.obd_ipccspintr = IPCC_SPINTR_SP3_AGPIO139,
		},
		.obdef_iomux = {
			/* UART0 - Console */
			IOMUX_CFG_ENTRY(135, MILAN_FCH_IOMUX_135_UART0_CTS_L),
			IOMUX_CFG_ENTRY(136, MILAN_FCH_IOMUX_136_UART0_RXD),
			IOMUX_CFG_ENTRY(137, MILAN_FCH_IOMUX_137_UART0_RTS_L),
			IOMUX_CFG_ENTRY(138, MILAN_FCH_IOMUX_138_UART0_TXD),
			/* UART1 - IPCC */
			IOMUX_CFG_ENTRY(140, MILAN_FCH_IOMUX_140_UART1_CTS_L),
			IOMUX_CFG_ENTRY(141, MILAN_FCH_IOMUX_141_UART1_RXD),
			IOMUX_CFG_ENTRY(142, MILAN_FCH_IOMUX_142_UART1_RTS_L),
			IOMUX_CFG_ENTRY(143, MILAN_FCH_IOMUX_143_UART1_TXD),
			/* SP_TO_SP3_INT_L */
			IOMUX_CFG_ENTRY(139, MILAN_FCH_IOMUX_139_AGPIO139)
		},
		.obdef_tests = {
			{
				.obt_func = eb_eval_socket,
				.obt_socket = X86_SOCKET_SP3
			}, {
				.obt_func = eb_eval_chiprev,
				.obt_chiprev = {
					X86_CHIPREV_AMD_MILAN_B0 |
					X86_CHIPREV_AMD_MILAN_B1
				}
			}, {
				/*
				 * We determine if this is a gimlet by
				 * inspecting GPIO129 to determine whether it
				 * is floating (not connected). On Ethanol-X it
				 * is always driven high.
				 *
				 * The tests are run in order so by the time
				 * we are here we know this is a Milan chip in
				 * an SP3 socket.
				 */
				.obt_func = eb_eval_gpio_tristate,
				.obt_tristate = {
					.otgt_gpionum = 129,
					/*
					 * Before changing the iomux for a GPIO
					 * that shares a pin with KBRST_L, or
					 * adjusting its state, we must clear
					 * FCH::PM::RESETCONTROL1[kbrsten] to
					 * avoid resetting ourselves.
					 */
					.otgt_init = eb_disable_kbrst,
					.otgt_fini = eb_enable_kbrst,
					.otgt_iomux = IOMUX_CFG_ENTRY(129,
					    MILAN_FCH_IOMUX_129_AGPIO129),
					.otgt_expect = {
						.ogt_floating = OGS_DONTCARE,
						.ogt_pulledup = OGS_HIGH,
						.ogt_pulleddown = OGS_LOW
					}
				}
			}
		}
	}, {
		.obdef_board_data = {
			.obd_board = OXIDE_BOARD_ETHANOLX,
			.obd_rootnexus = "Oxide,Ethanol-X",
			.obd_ipccmode = IPCC_MODE_DISABLED,
			.obd_startupopts = IPCC_STARTUP_KMDB_BOOT |
			    IPCC_STARTUP_VERBOSE | IPCC_STARTUP_PROM
		},
		.obdef_iomux = {
			/* UART0 - Console */
			IOMUX_CFG_ENTRY(135, MILAN_FCH_IOMUX_135_UART0_CTS_L),
			IOMUX_CFG_ENTRY(136, MILAN_FCH_IOMUX_136_UART0_RXD),
			IOMUX_CFG_ENTRY(137, MILAN_FCH_IOMUX_137_UART0_RTS_L),
			IOMUX_CFG_ENTRY(138, MILAN_FCH_IOMUX_138_UART0_TXD),
		},
		.obdef_tests = {
			{
				.obt_func = eb_eval_socket,
				.obt_socket = X86_SOCKET_SP3
			}, {
				.obt_func = eb_eval_chiprev,
				.obt_chiprev = {
					X86_CHIPREV_AMD_MILAN_ANY
				}
			}
		}
	}, {
		.obdef_board_data = {
			.obd_board = OXIDE_BOARD_RUBYRED,
			.obd_rootnexus = "Oxide,RubyRed",
			.obd_ipccmode = IPCC_MODE_DISABLED,
			.obd_startupopts = IPCC_STARTUP_KMDB_BOOT |
			    IPCC_STARTUP_VERBOSE | IPCC_STARTUP_PROM,
		},
		.obdef_iomux = {
			/* UART0 - Console */
			IOMUX_CFG_ENTRY(135, TURIN_FCH_IOMUX_135_UART0_CTS_L),
			IOMUX_CFG_ENTRY(136, TURIN_FCH_IOMUX_136_UART0_RXD),
			IOMUX_CFG_ENTRY(137, TURIN_FCH_IOMUX_137_UART0_RTS_L),
			IOMUX_CFG_ENTRY(138, TURIN_FCH_IOMUX_138_UART0_TXD),
		},
		.obdef_tests = {
			{
				.obt_func = eb_eval_socket,
				.obt_socket = X86_SOCKET_SP5
			}, {
				.obt_func = eb_eval_chiprev,
				.obt_chiprev = {
					X86_CHIPREV_AMD_TURIN_ANY,
					X86_CHIPREV_AMD_DENSE_TURIN_ANY
				}
			}, {
				/*
				 * We determine if this is a RubyRed by
				 * checking the bootrom type that was selected
				 * by strap pins.
				 */
				.obt_func = eb_eval_romtype,
				.obt_romtype =
				    FCH_MISC_A_STRAPSTATUS_ROMTYPE_ESPI_SAFS
			}
		}
	},
	/*
	 * Although we could combine the following two Ruby entries into one --
	 * covering Genoa, Turin and Dense Turin -- they are currently separate
	 * so that the appropriate FCH_IOMUX definitions are used for each
	 * processor type since these are currently defined per CPU type.
	 * Future consolidation may change that and we should revisit this
	 * then.
	 */
	{
		.obdef_board_data = {
			.obd_board = OXIDE_BOARD_RUBY,
			.obd_rootnexus = "Oxide,Ruby",
			.obd_ipccmode = IPCC_MODE_DISABLED,
			.obd_startupopts = IPCC_STARTUP_KMDB_BOOT |
			    IPCC_STARTUP_VERBOSE | IPCC_STARTUP_PROM
		},
		.obdef_iomux = {
			/* UART0 - Console */
			IOMUX_CFG_ENTRY(135, GENOA_FCH_IOMUX_135_UART0_CTS_L),
			IOMUX_CFG_ENTRY(136, GENOA_FCH_IOMUX_136_UART0_RXD),
			IOMUX_CFG_ENTRY(137, GENOA_FCH_IOMUX_137_UART0_RTS_L),
			IOMUX_CFG_ENTRY(138, GENOA_FCH_IOMUX_138_UART0_TXD),
		},
		.obdef_tests = {
			{
				.obt_func = eb_eval_socket,
				.obt_socket = X86_SOCKET_SP5
			}, {
				.obt_func = eb_eval_chiprev,
				.obt_chiprev = {
					X86_CHIPREV_AMD_GENOA_ANY
				}
			}
		}
	}, {
		.obdef_board_data = {
			.obd_board = OXIDE_BOARD_RUBY,
			.obd_rootnexus = "Oxide,Ruby",
			.obd_ipccmode = IPCC_MODE_DISABLED,
			.obd_startupopts = IPCC_STARTUP_KMDB_BOOT |
			    IPCC_STARTUP_VERBOSE | IPCC_STARTUP_PROM
		},
		.obdef_iomux = {
			/* UART0 - Console */
			IOMUX_CFG_ENTRY(135, TURIN_FCH_IOMUX_135_UART0_CTS_L),
			IOMUX_CFG_ENTRY(136, TURIN_FCH_IOMUX_136_UART0_RXD),
			IOMUX_CFG_ENTRY(137, TURIN_FCH_IOMUX_137_UART0_RTS_L),
			IOMUX_CFG_ENTRY(138, TURIN_FCH_IOMUX_138_UART0_TXD),
		},
		.obdef_tests = {
			{
				.obt_func = eb_eval_socket,
				.obt_socket = X86_SOCKET_SP5
			}, {
				.obt_func = eb_eval_chiprev,
				.obt_chiprev = {
					X86_CHIPREV_AMD_TURIN_ANY,
					X86_CHIPREV_AMD_DENSE_TURIN_ANY
				}
			}
		}
	}, {
		.obdef_board_data = {
			.obd_board = OXIDE_BOARD_UNKNOWN,
			.obd_rootnexus = "Oxide,Unknown",
			.obd_ipccmode = IPCC_MODE_DISABLED,
			.obd_startupopts = IPCC_STARTUP_KMDB_BOOT |
			    IPCC_STARTUP_VERBOSE | IPCC_STARTUP_PROM
		}
	}
};

static bool
eb_eval_socket(const oxide_board_cpuinfo_t *cpuinfo,
    const oxide_board_test_t *test, oxide_board_testresult_t *result __unused)
{
	return (cpuinfo->obc_socket == test->obt_socket);
}

static bool
eb_eval_chiprev(const oxide_board_cpuinfo_t *cpuinfo,
    const oxide_board_test_t *test, oxide_board_testresult_t *result __unused)
{
	for (uint_t i = 0; i < ARRAY_SIZE(test->obt_chiprev); i++) {
		x86_chiprev_t rev = test->obt_chiprev[i];

		if (rev == X86_CHIPREV_UNKNOWN)
			break;

		if (chiprev_matches(cpuinfo->obc_chiprev, rev))
			return (true);
	}

	return (false);
}

/*
 * Test the state of a GPIO input in three states; with no internal pulls
 * enabled, with an internal pullup and with an internal pulldown.
 *
 * Note that we are assuming that the time between writing and reading back
 * the GPIO register is enough for the weak internal pull to accumulate
 * sufficient charge on the input gate to reach the required detection
 * threshold.
 */
static bool
eb_eval_gpio_tristate(const oxide_board_cpuinfo_t *cpuinfo,
    const oxide_board_test_t *test, oxide_board_testresult_t *result)
{
	const oxide_test_gpio_tristate_t *gts = &test->obt_tristate;
	const oxide_gpio_tristate_t *tse = &gts->otgt_expect;
	oxide_gpio_tristate_t *tsr = &result->obtr_tristate;
	uint32_t orig_val, val;
	uint32_t orig_iomux = 0;
	mmio_reg_block_t iomux_block;
	mmio_reg_t iomux;

	if (gts->otgt_init != NULL)
		gts->otgt_init(cpuinfo, test);

	mmio_reg_block_t block = fch_gpio_mmio_block();
	mmio_reg_t reg = FCH_GPIO_GPIO_MMIO(block, gts->otgt_gpionum);

	/*
	 * Configure the GPIO with a known initial state, prior to setting
	 * any iomux below.
	 */
	orig_val = val = mmio_reg_read(reg);
	val = FCH_GPIO_GPIO_SET_OUT_EN(val, 0);
	val = FCH_GPIO_GPIO_SET_OUTPUT(val, 0);
	val = FCH_GPIO_GPIO_SET_PD_EN(val, 0);
	val = FCH_GPIO_GPIO_SET_PU_EN(val, 0);
	val = FCH_GPIO_GPIO_SET_WAKE_S5(val, 0);
	val = FCH_GPIO_GPIO_SET_WAKE_S3(val, 0);
	val = FCH_GPIO_GPIO_SET_WAKE_S0I3(val, 0);
	val = FCH_GPIO_GPIO_SET_INT_EN(val, 0);
	val = FCH_GPIO_GPIO_SET_INT_STS_EN(val, 0);
	mmio_reg_write(reg, val);

	if (gts->otgt_iomux.obp_valid) {
		iomux_block = fch_iomux_mmio_block();
		iomux = FCH_IOMUX_IOMUX_MMIO(iomux_block,
		    gts->otgt_iomux.obp_gpio);

		orig_iomux = mmio_reg_read(iomux);
		mmio_reg_write(iomux, gts->otgt_iomux.obp_iomux);
	}

	/* Test with the internal pullup enabled */
	val = mmio_reg_read(reg);
	val = FCH_GPIO_GPIO_SET_PU_EN(val, 1);
	val = FCH_GPIO_GPIO_SET_PD_EN(val, 0);
	mmio_reg_write(reg, val);
	val = mmio_reg_read(reg);
	tsr->ogt_pulledup =
	    FCH_GPIO_GPIO_GET_INPUT(val) == FCH_GPIO_GPIO_INPUT_HIGH ?
	    OGS_HIGH : OGS_LOW;

	/* Test with the internal pulldown enabled */
	val = FCH_GPIO_GPIO_SET_PU_EN(val, 0);
	val = FCH_GPIO_GPIO_SET_PD_EN(val, 1);
	mmio_reg_write(reg, val);
	val = mmio_reg_read(reg);
	tsr->ogt_pulleddown =
	    FCH_GPIO_GPIO_GET_INPUT(val) == FCH_GPIO_GPIO_INPUT_HIGH ?
	    OGS_HIGH : OGS_LOW;

	/* Test with no pulldowns */
	val = FCH_GPIO_GPIO_SET_PU_EN(val, 0);
	val = FCH_GPIO_GPIO_SET_PD_EN(val, 0);
	mmio_reg_write(reg, val);
	val = mmio_reg_read(reg);
	tsr->ogt_floating =
	    FCH_GPIO_GPIO_GET_INPUT(val) == FCH_GPIO_GPIO_INPUT_HIGH ?
	    OGS_HIGH : OGS_LOW;

	/* Reset the GPIO to the state it had when we began */
	mmio_reg_write(reg, orig_val);
	mmio_reg_block_unmap(&block);

	/* Reset the IO/MUX to the original state */
	if (gts->otgt_iomux.obp_valid) {
		mmio_reg_write(iomux, orig_iomux);
		mmio_reg_block_unmap(&iomux_block);
	}

	if (gts->otgt_fini != NULL)
		gts->otgt_fini(cpuinfo, test);

	return (
	    (tse->ogt_floating == OGS_DONTCARE ||
	    tse->ogt_floating == tsr->ogt_floating) &&
	    (tse->ogt_pulledup == OGS_DONTCARE ||
	    tse->ogt_pulledup == tsr->ogt_pulledup) &&
	    (tse->ogt_pulleddown == OGS_DONTCARE ||
	    tse->ogt_pulleddown == tsr->ogt_pulleddown));
}

/*
 * Check the boot rom selection straps that are cached in
 * FCH::MISC::STRAPSTATUS against the desired value.
 */
static bool
eb_eval_romtype(const oxide_board_cpuinfo_t *cpuinfo,
    const oxide_board_test_t *test, oxide_board_testresult_t *result)
{
	mmio_reg_block_t fch_misc_a;
	mmio_reg_t reg;
	uint32_t val;

	fch_misc_a = fch_misc_a_mmio_block();
	reg = FCH_MISC_A_STRAPSTATUS_MMIO(fch_misc_a);
	val = mmio_reg_read(reg);
	result->obtr_romtype = FCH_MISC_A_STRAPSTATUS_GET_ROMTYPE(val);
	mmio_reg_block_unmap(&fch_misc_a);

	return (result->obtr_romtype == test->obt_romtype);
}

static void
eb_set_kbrst(bool state)
{
	mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
	mmio_reg_t rstctl_reg = FCH_PMIO_RESETCONTROL1_MMIO(fch_pmio);
	uint32_t rstctl_val = mmio_reg_read(rstctl_reg);

	rstctl_val = FCH_PMIO_RESETCONTROL1_SET_KBRSTEN(rstctl_val,
	    state ? 1 : 0);
	mmio_reg_write(rstctl_reg, rstctl_val);
	mmio_reg_block_unmap(&fch_pmio);
}

static void
eb_disable_kbrst(const oxide_board_cpuinfo_t *cpuinfo __unused,
    const oxide_board_test_t *test __unused)
{
	eb_set_kbrst(false);
}

static void
eb_enable_kbrst(const oxide_board_cpuinfo_t *cpuinfo __unused,
    const oxide_board_test_t *test __unused)
{
	eb_set_kbrst(true);
}

static x86_chiprev_t
early_cpuid_ident(uint_t *familyp, uint_t *modelp, uint_t *steppingp)
{
	struct cpuid_regs cpu_regs = { 0 };
	uint_t family, model, stepping;

	cpu_regs.cp_eax = 1;
	(void) __cpuid_insn(&cpu_regs);

	family = CPUID_XFAMILY(cpu_regs.cp_eax);
	model = CPUID_XMODEL(cpu_regs.cp_eax);
	stepping = CPUID_XSTEPPING(cpu_regs.cp_eax);

	/*
	 * We only support AMD processors which use the extended model
	 * iff the base family is 0xf.
	 */
	if (family == 0xf) {
		family += CPUID_XFAMILY_XTD(cpu_regs.cp_eax);
		model +=
		    CPUID_XMODEL_XTD(cpu_regs.cp_eax) << CPUID_XMODEL_XTD_SHIFT;
	}

	*familyp = family;
	*modelp = model;
	*steppingp = stepping;

	return (_cpuid_chiprev(X86_VENDOR_AMD, family, model, stepping));
}

static void
oxide_board_iomux_setup(oxide_board_def_t *b)
{
	mmio_reg_block_t block;

	if (!b->obdef_iomux[0].obp_valid)
		return;

	block = fch_iomux_mmio_block();

	for (uint_t i = 0; i < ARRAY_SIZE(b->obdef_iomux); i++) {
		const oxide_board_iomux_t *pm = &b->obdef_iomux[i];

		if (!pm->obp_valid)
			break;

		mmio_reg_write(FCH_IOMUX_IOMUX_MMIO(block, pm->obp_gpio),
		    pm->obp_iomux);
	}

	mmio_reg_block_unmap(&block);
}

static const char *
oxide_board_name(oxide_board_t board)
{
	switch (board) {
	case OXIDE_BOARD_GIMLET:
		return ("Gimlet");
	case OXIDE_BOARD_ETHANOLX:
		return ("Ethanol-X");
	case OXIDE_BOARD_COSMO:
		return ("Cosmo");
	case OXIDE_BOARD_RUBY:
		return ("Ruby");
	case OXIDE_BOARD_RUBYRED:
		return ("RubyRed");
	case OXIDE_BOARD_UNKNOWN:
	default:
		return ("Unknown");
	}
}

void
oxide_derive_platform(void)
{
	uint_t family, model, stepping;
	oxide_board_cpuinfo_t cpuinfo;

	/*
	 * We don't support running in a virtual environment so we disable
	 * platform detection entirely. We still need to call
	 * determine_platform() as that's responsible for setting the platform
	 * type, and that must be done prior to calling _cpuid_skt()
	 */
	enable_platform_detection = 0;
	determine_platform();

	cpuinfo.obc_chiprev = early_cpuid_ident(&family, &model, &stepping);
	cpuinfo.obc_chiprevstr = _cpuid_chiprevstr(
	    X86_VENDOR_AMD, family, model, stepping);
	cpuinfo.obc_uarchrev = _cpuid_uarchrev(X86_VENDOR_AMD, family, model,
	    stepping);
	cpuinfo.obc_socket = _cpuid_skt(X86_VENDOR_AMD, family, model,
	    stepping);
	cpuinfo.obc_fchkind = chiprev_fch_kind(cpuinfo.obc_chiprev);

	for (uint_t i = 0; i < ARRAY_SIZE(oxide_board_defs); i++) {
		oxide_board_def_t *b = &oxide_board_defs[i];
		bool match = true;

		for (uint_t j = 0; j < ARRAY_SIZE(b->obdef_tests); j++) {
			if (b->obdef_tests[j].obt_func == NULL)
				break;
			if (!b->obdef_tests[j].obt_func(&cpuinfo,
			    &b->obdef_tests[j], &b->obdef_testresults[j])) {
				match = false;
				break;
			}
		}

		if (match) {
			oxide_board_data_t *data = &b->obdef_board_data;

			data->obd_cpuinfo = cpuinfo;
			oxide_board_iomux_setup(b);

			switch (_X86_CHIPREV_FAMILY(cpuinfo.obc_chiprev)) {
			case X86_PF_AMD_MILAN:
				data->obd_zen_platform = &milan_platform;
				break;
			case X86_PF_AMD_GENOA:
				data->obd_zen_platform = &genoa_platform;
				break;
			case X86_PF_AMD_TURIN:
				data->obd_zen_platform = &turin_platform;
				break;
			case X86_PF_AMD_DENSE_TURIN:
				data->obd_zen_platform = &dense_turin_platform;
				break;
			default:
				bop_printf(NULL, "Oxide board %s -- %s\n",
				    oxide_board_name(data->obd_board),
				    data->obd_cpuinfo.obc_chiprevstr);
				bop_panic("Unsupported processor family");
			}

			oxide_board_data = data;
			break;
		}
	}

	if (oxide_board_data == NULL)
		bop_panic("Could not derive Oxide board type");
}

bool
oxide_board_is_ruby(void)
{
	VERIFY(oxide_board_data != NULL);
	return (oxide_board_data->obd_board == OXIDE_BOARD_RUBY);
}

void
oxide_report_platform(void)
{
	bop_printf(NULL, "Oxide board %s -- %s\n",
	    oxide_board_name(oxide_board_data->obd_board),
	    oxide_board_data->obd_cpuinfo.obc_chiprevstr);
}
