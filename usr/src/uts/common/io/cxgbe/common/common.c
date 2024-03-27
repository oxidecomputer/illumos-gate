#include "t4_hw.h"
#include "t4_chip_type.h"
#include "common.h"

unsigned int dack_ticks_to_usec(const struct adapter *adap,
					      unsigned int ticks)
{
	return (ticks << adap->params.tp.dack_re) / core_ticks_per_usec(adap);
}

unsigned int us_to_core_ticks(const struct adapter *adap,
					    unsigned int us)
{
	return (us * adap->params.vpd.cclk) / 1000;
}

int is_t4(enum chip_type chip)
{
	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T4);
}

int is_t5(enum chip_type chip)
{

	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T5);
}

int is_t6(enum chip_type chip)
{
	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T6);
}

int is_t7(enum chip_type chip)
{
	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T7);
}

int is_fpga(enum chip_type chip)
{
	 return chip & CHELSIO_CHIP_FPGA;
}

unsigned int core_ticks_per_usec(const struct adapter *adap)
{
	return adap->params.vpd.cclk / 1000;
}
