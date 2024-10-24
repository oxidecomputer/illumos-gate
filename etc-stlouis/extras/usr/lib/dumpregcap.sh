#!/bin/ksh

#
# A DEBUG kernel snapshots various PCIe core and port registers at various
# stages of initialisation, and this script extracts and dumps those from a
# live system or a crash dump.
#

typeset -r DEV="$1"
typeset -r DUMP="${2:--k}"
typeset -r MDB="mdb ${DUMP}"

function fatal {
	echo "$*" >&2
	exit 1
}

typeset -A gimlet_map=(
	[N0]=( nbio=0 coreno=0 portno=0)
	[N1]=( nbio=0 coreno=0 portno=1)
	[N2]=( nbio=0 coreno=0 portno=2)
	[N3]=( nbio=0 coreno=0 portno=3)
	[N4]=( nbio=3 coreno=1 portno=3)
	[N5]=( nbio=3 coreno=1 portno=2)
	[N6]=( nbio=3 coreno=1 portno=1)
	[N7]=( nbio=2 coreno=0 portno=2)
	[N8]=( nbio=2 coreno=0 portno=1)
	[N9]=( nbio=2 coreno=0 portno=0)
	[T6]=( nbio=1 coreno=0 portno=0)
	[M2A]=(nbio=3 coreno=0 portno=2)
	[M2B]=(nbio=2 coreno=1 portno=2)
	[RSW]=(nbio=0 coreno=1 portno=1)
)

typeset -A systems=(
	[Oxide,Gimlet]=gimlet_map
)

typeset -r system=$($MDB -e '::prtconf -p' | awk 'NR==2{print $2}')

print "Identified system as: $system"

[[ -n "${systems[$system]}" ]] || fatal "Unhandled system '$system'"

nameref sysdata=${systems[$system]}

function usage {
	{
	    print "Usage: $0 <device> [ idx ]"
	    print "  idx = dump number to extract from; elide for live state"
	    print "  Available devices: ${!sysdata[@]}"
	} >&2
	exit 1
}

[[ -z "${sysdata[$DEV]}" ]] && usage

typeset -ri nbio=${sysdata[$DEV].nbio}
typeset -ri coreno=${sysdata[$DEV].coreno}
typeset -ri portno=${sysdata[$DEV].portno}

AWKPROG='
/^[ 	]*zprd_name/ {
	gsub("\"", "", $4)
	printf("%-48s", $4)
}

/^[ 	]*zprd_val/ {
	gsub(",", "", $0)
	gsub("0x", "", $0)
	printf(" %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
	       $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)
}'

sn=$(${MDB} -e "::prtconf -v -d rootnex ! grep -A 1 baseboard-identifier" |\
    tail -1 | cut -d= -f2 | sed -e "s/\'//g")
if [[ -z "${sn}" ]]; then
	printf "WARNING: no board serial number found\n" >&2
	sn="<unknown>"
fi
uname=$(${MDB} -e "utsname::printf %s struct utsname version")
if [[ -z "${uname}" ]]; then
	printf "WARNING: no kernel version found\n" >&2
	uname="<unknown>"
fi

printf "Kernel: ${uname} Board: ${sn} Device: ${DEV}\n"
printf "\n"

CORE="zf_socs[0].zs_iodies[0].zi_ioms[${nbio}].zio_pcie_cores[${coreno}]"
PORT="${CORE}.zpc_ports[${portno}]"

coredbg=$(${MDB} -e "zen_fabric::print ${CORE}.zpc_dbg | =K")

if [[ ${coredbg} == 0 ]]; then
	printf "no PCIe core debug state for ${DEV}\n" >&2
else
	regs=$(${MDB} -e "${coredbg}::print -a zen_pcie_dbg_t zpd_regs | =K")
	nregs=$(${MDB} -e "${coredbg}::print zen_pcie_dbg_t zpd_nregs | =X")
	${MDB} -e "${regs},${nregs}::print zen_pcie_reg_dbg_t" | \
	    awk "${AWKPROG}"
fi

portdbg=$(${MDB} -e "zen_fabric::print ${PORT}.zpp_dbg | =K")

if [[ ${portdbg} == 0 ]]; then
	printf "no PCIe port debug state available for ${DEV}\n" >&2
else
	regs=$(${MDB} -e "${portdbg}::print -a zen_pcie_dbg_t zpd_regs | =K")
	nregs=$(${MDB} -e "${portdbg}::print zen_pcie_dbg_t zpd_nregs | =X")
	${MDB} -e "${regs},${nregs}::print zen_pcie_reg_dbg_t" | \
	    awk "${AWKPROG}"
fi
