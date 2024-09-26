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

/*
 * Various routines and things to access, initialize, understand, and manage
 * Milan's I/O fabric. This consists of both the data fabric and the
 * northbridges.
 *
 * --------------------------------------
 * Physical Organization and Nomenclature
 * --------------------------------------
 *
 * In AMD's Zen 2 and 3 designs, the CPU socket is organized as a series of
 * chiplets with a series of compute complexes and then a central I/O die.
 * uts/intel/os/cpuid.c has an example of what this looks like. Critically, this
 * I/O die is the major device that we are concerned with here as it bridges the
 * cores to basically the outside world through a combination of different
 * devices and I/O paths.  The part of the I/O die that we will spend most of
 * our time dealing with is the "northbridge I/O unit", or NBIO.  In DF (Zen
 * data fabric) terms, NBIOs are a class of device called an IOMS (I/O
 * master-slave).  These are represented in our fabric data structures as
 * subordinate to an I/O die.  On Milan processors, each I/O die has 4 NBIO
 * instances; other processor families have these in differing number or
 * organisation.  Since we're interested in Zen 3 here (and since Zen 2 and 4
 * are very similar), let's expand the I/O Die portion of the Zen 2 diagram from
 * cpuid.c:
 *
 *                      P  P  P  data fabric  P     P
 *                      P  P  P       |       P     P
 *             +--------P--P--P-------|-------P-----P--------+
 *             |        P  P  P       |       P     P        |
 *             |    +-------------+   |   +-------------+    |
 *             |    |             |   |   |             |    |
 *             |    |   NBIO 0    +---+---+   NBIO 1    |    |
 *             |    |   (IOMS)    |   |   |   (IOMS)    |    |
 *             |    |             |   |   |             |    |
 *             |    +-------------+   |   +-------------+    |
 *             |                      |                      |
 *             |                      |                      |
 *             |    +-------------+   |   +-------------+    |
 *         MMMMMMMMM|     UMC     +---+---+     UMC     |    |
 *             |    |    (CS)     |   |   |    (CS)     |MMMMMMMMM
 *             |    +-------------+   |   +-------------+    |
 *             |                      |                      |
 *             |    +-------------+   |   +-------------+    |
 *         MMMMMMMMM|     UMC     +---+---+     UMC     |    |
 *             |    |    (CS)     |   |   |    (CS)     |MMMMMMMMM
 *             |    +-------------+   |   +-------------+    |
 *             |                      |                      |
 *             |    +-------------+   |                      |
 *             |    |     MP0     |   |                      |
 *             |    +-------------+   |                      |
 *             |                      |                      |
 *             |    +-------------+   |                      |
 *             |    |     MP1     |   |                      |
 *             |    +-------------+   |                      |
 *             |                      |                      |
 *             |    +-------------+   |   +-------------+    |
 *         MMMMMMMMM|     UMC     |   |   |     UMC     |    |
 *                  |    (CS)     +---+---+    (CS)     |MMMMMMMMM
 *             |    +-------------+   |   +-------------+    |
 *             |                      |                      |
 *             |    +-------------+   |   +-------------+    |
 *         MMMMMMMMM|     UMC     |   |   |     UMC     |    |
 *                  |    (CS)     +---+---+    (CS)     |MMMMMMMMM
 *             |    +-------------+   |   +-------------+    |
 *             |                      |                      |
 *             |                      |                      |
 *             |                      |   +-------------+    |
 *             |                      |   |     FCH     |    |
 *             |                      |   +------+------+    |
 *             |                      |          |           |
 *             |    +-------------+   |   +------+------+    |
 *             |    |             |   |   |             |    |
 *             |    |   NBIO 2    |   |   |   NBIO 3    |    |
 *             |    |   (IOMS)    +---+---+   (IOMS)    |    |
 *             |    |             |   |   |             |    |
 *             |    +-------------+   |   +-------------+    |
 *             |        P     P       |       P     P        |
 *             +--------P-----P-------|-------P-----P--------+
 *                      P     P       |       P     P
 *                               DF to second
 *                              socket via xGMI
 *
 * Each NBIO instance implements, among other things, a PCIe root complex (RC),
 * consisting of two major components: an I/O hub core (IOHC) that implements
 * the host side of the RC, and two or three PCIe cores that implement the PCIe
 * side.  The IOHC appears in PCI configuration space as a root complex and is
 * the attachment point for npe(4d).  The PCIe cores do not themselves appear in
 * config space; however, each implements up to 8 PCIe root ports, and each root
 * port has an associated host bridge that appears in configuration space.
 * Externally-attached PCIe devices are enumerated under these bridges, and the
 * bridge provides the standard PCIe interface to the downstream port including
 * link status and control.
 *
 * Two of the NBIO instances are somewhat special and merit brief additional
 * discussion.  Instance 0 has a third PCIe core, which is associated with the 2
 * lanes that would otherwise be used for WAFL, and can form either 2 x1 ports
 * or a single x2 port.  Instance 3 has the Fusion Controller Hub (FCH) attached
 * to it; the FCH doesn't contain any real PCIe devices, but it does contain
 * some fake ones and from what we can tell the NBIO is the DF endpoint where
 * MMIO transactions targeting the FCH are directed.
 *
 * The UMCs are instances of CS (coherent slave) DF components; we do not
 * discuss them further here, but details may be found in
 * uts/intel/sys/amdzen/umc.h and uts/intel/io/amdzen/zen_umc.c.
 *
 * This is still a grossly simplified diagram: WAFL (GMI-over-PCIe x1) and xGMI
 * (GMI-over-PCIe x16) are merely protocols sitting atop PCIe phys.  Each lane
 * has an entire collection of phy-related logic that is also part of the I/O
 * die but not part of the NBIO; this layer is known as direct crossbar I/O
 * (DXIO), and contains logic that can multiplex a subset of the phys among
 * protocols, including SATA if so configured.  WAFL and xGMI are used only in
 * 2-socket (2S) configurations such as the Ethanol-X reference board supported
 * by this code; these protocols and their phys are set up before we gain
 * control, which conveniently allows us to access the remote socket as part of
 * a single DF.  We do not support SATA at all, even on Ethanol-X which
 * implements it in hardware, so it's not discussed further.  In addition to the
 * extra complexity toward the periphery, there is also some additional
 * complexity toward the interior: each component on the DF has a block of logic
 * called a scalable data port (SDP) that provides the interface between the
 * component and the DF.  Independent of this, at least conceptually, is the
 * system management network (SMN, also called the scalable control fabric),
 * used to access most of the logic in these components; each SMN endpoint also
 * contains a remote system management unit (RSMU) that manages the control
 * interface.  SMN has its own address space entirely separate from the "main"
 * (RAM, MMIO, etc.) address space routed over the DF, and the level of
 * granularity associated with SMN endpoints and RSMUs is much finer than the
 * level associated with DF components.  Additional detail on the SMN may be
 * found in uts/intel/sys/amdzen/smn.h.  There are undoubtedly yet more layers
 * so undocumented that we remain ignorant of their existence, never mind their
 * function.
 *
 * With all that in mind, let's zoom in one more time on the part of the I/O die
 * around one of the typical NBIO instances:
 *
 *               SMN                                         DF
 *                |                                          |
 *         ~ ~ ~ ~|~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ | ~ ~
 *             |  |   I/O die                                |
 *             |  |                                          |
 *             |  |  +------------------------------------+  |
 *             |  |  | NBIO 2                             |  |
 *             |  |  |                                    |  |
 *             |  |  |         +---------------+          |  |
 *             |  |  |  +------|               |-----+    |  |
 *             |  +-----+ RSMU |     IOHC      | SDP +-------+
 *             |  |  |  +------|               |-----+    |  |
 *             |  |  |         +----+---------++          |  |
 *             |  |  |              |         |           |  |
 *             |  |  |              |         |           |  |
 *             |  |  |         +----+------+  |           |  |
 *             |  |  |  +------|   PCIe    |  |           |  |
 *             |  +-----+ RSMU |  Core 0   |  |           |  |
 *             |  |  |  +------|-----------+  |           |  |
 *             |  |  |         | P | P |   |  |           |  |
 *             |  |  |         | o | o | . |  |           |  |
 *             |  |  |         | r | r | . |  |           |  |
 *             |  |  |         | t | t | . |  |           |  |
 *             |  |  |         |   |   |   |  |           |  |
 *             |  |  |         | A | B |   |  |           |  |
 *             |  |  |         +-+-+---+---+  |           |  |
 *             |  |  |           |            |           |  |
 *             |  |  |        +--+            |           |  |
 *             |  |  |        |          +----+------+    |  |
 *             |  |  |        |   +------|   PCIe    |    |  |
 *             |  +---------------+ RSMU |  Core 1   |    |  |
 *             |  |  |        |   +------|-----------+    |  |
 *             |  |  |        |          | P | P |   |    |  |
 *             |  |  |        |          | o | o | . |    |  |
 *             |  |  |        |          | r | r | . |    |  |
 *             |  |  |        |          | t | t | . |    |  |
 *             |  |  |        |          |   |   |   |    |  |
 *             |  |  |        |          | A | B |   |    |  |
 *             |  |  |        +--+       +-+-+-+-+---+    |  |
 *             |  |  |           |         |   |          |  |
 *             |  |  +-----------|---------|---|----------+  |
 *             |  |              |         |   |             |
 *             |  |            +-+---------+---+-----+       |
 *             |  |     +------|                     |       |
 *             |  +-----+ RSMU |        DXIO         |       |
 *             |        +------|                     |       |
 *             |               +---------------------+       |
 *             |               | P | P | P | P |     |       |
 *             |               | h | h | h | h |  .  |       |
 *             |               | y | y | y | y |  .  |       |
 *             |               |   |   |   |   |  .  |       |
 *             |               | 0 | 1 | 2 | 3 |     |       |
 *             +---------------+---+---+---+---+-----+-------+
 *                               P   P   P   P
 *                               P   P   P   P
 *
 * While the detail of the DXIO subsystem is not shown here, diagrams may be
 * found in chapter 16 of the PPR.  There are also components including the
 * IOAGR and IOMMU that exist in each NBIO but are not shown here.  The SDP also
 * has some additional components, including a mux that we configure in the code
 * below.  The intent here is not to replicate the PPR but to give the reader a
 * high-level sense of how these components fit together and correspond to the
 * data structures we employ.  One additional component of the NBIO merits a
 * brief mention: NBIFs (northbridge interfaces) are effectively peers of PCIe
 * cores; however, they do not have an externally-visible port or phys
 * associated with them.  Instead, they are connected internally to other logic
 * on the I/O die that provides peripherals such as SATA and USB controllers.
 * These devices appear in PCI configuration space and are enumerated as true
 * PCIe devices; they even have link control and status capabilities like a PCIe
 * device with a port would.  We perform minimal configuration of the NBIFs; the
 * peripherals to which they provide access are not supported on this
 * architecture.
 *
 * --------------
 * Representation
 * --------------
 *
 * We represent the NBIO entities described above and the CPU core entities
 * described in cpuid.c in a hierarchical fashion:
 *
 * zen_fabric_t (DF -- root)
 * |
 * \-- zen_soc_t (qty 1 or 2)
 *     |
 *     \-- zen_iodie_t (qty 1)
 *         |
 *         +-- zen_ioms_t (qty 4, one per NBIO)
 *         |   |
 *         |   +-- zen_pcie_core_t (qty 2, except 3 for IOMS 0)
 *         |   |   |
 *         |   |   \-- zen_pcie_port_t (qty 8, except 2 for IOMS 0 RC 2)
 *         |   |
 *         |   \-- zen_nbif_t (qty 3 + 2 in "alternate space")
 *         |
 *         \-- zen_ccd_t (qty varies 1-8)
 *             |
 *             \-- zen_ccx_t (qty 1)
 *                 |
 *                 \-- zen_core_t (qty varies, 4-8)
 *                     |
 *                     \-- zen_thread_t (qty 2, unless SMT is disabled)
 *
 * The PCIe bridge does not have its own representation in this schema, but is
 * represented as a B/D/F associated with a PCIe port.  That B/D/F provides the
 * standard PCIe bridge interfaces associated with a root port and host bridge.
 *
 * For our purposes, each PCIe core is associated with an instance of the
 * PCIECORE register block and an RSMU (remote system management unit) register
 * block.  These implementation-specific registers control the PCIe core logic.
 * Each root port is associated with an instance of the PCIEPORT register block
 * and the standard PCIe-defined registers of the host bridge which AMD refers
 * to as PCIERCCFG.  Note that the MP1 DXIO firmware also accesses at least some
 * of the PCIECORE, PCIEPORT, and the SMU::RSMU::RSMU::PCIE0::MMIOEXT registers,
 * and a limited set of fields in the standard bridge registers associated with
 * hotplug are controlled by that firmware as well, though the intent is that
 * they are controlled in standards-compliant ways.  These associations allow us
 * to obtain SMN register instances from a pointer to the entity to which those
 * registers pertain.
 *
 * ------------------
 * PCIe Configuration
 * ------------------
 *
 * AMD's implementation of PCIe configuration reflects their overall legacy
 * architecture: an early phase that they implement in UEFI firmware, and a
 * standard enumeration phase that is done by the UEFI userland application,
 * typically but not necessarily an "OS" like i86pc illumos.  For reasons of
 * expediency, we've taken a similar approach here, but it's not necessary to do
 * so, and some notes on possible future work may be found below.  This allows
 * us to reuse the pci_autoconfig (one-shot enumeration and resource assignment
 * at boot) and pciehp (hotplug controller management and runtime enumeration
 * and resource assignment) code already available for PCs.  That code isn't
 * really as generic as one might imagine; it makes a number of significant
 * assumptions based on the ideas that (a) this machine has firmware and (b) it
 * has done things that mostly conform to the PCIe Firmware Specification,
 * neither of which is accurate.  Fortunately, PC firmware is so commonly and
 * severely broken that those assumptions are not strongly held, and it's
 * possible to achieve more or less correct results even though little or none
 * of that is done here.  There are some very unfortunate consequences
 * associated with the one-shot approach to resource allocation that will be
 * discussed a bit more below, but first we'll discuss how a collection of
 * internal processor logic is configured to provide standard access to both
 * internal and external PCIe functions.  The remainder of this section is
 * applicable to underlying mechanism and our current implementation, which is
 * of course different from UEFI implementations.
 *
 * We have three basic goals during this part of PCIe configuration:
 *
 * 1. Construct the correct associations between the PCS (physical coding
 *    sublayer) and a collection of PCIe ports that are attached to a specific
 *    set of lanes routed on a given board to either chip-down devices or
 *    connectors to which other PCIe devices can be attached.
 *
 * 2. Set a large number of parameters governing the behaviour, both
 *    standardised and not, of each of the PCIe cores and ports.  This includes
 *    everything from what kind of error conditions are reported when specific
 *    events occur to how root complexes and host bridges identify themselves to
 *    standard PCIe software to how each host bridge's hotplug functionality (if
 *    any) is accessed.
 *
 * 3. Connect and route chunks of various address spaces from the amd64
 *    processor cores (and sometimes other logic as well!) to the appropriate
 *    PCIe root complex and host bridge.  This does not include assignment of
 *    MMIO and legacy I/O address blocks to bridges or downstream devices, but
 *    it does include allocating PCI bus numbers and top-level blocks of MMIO
 *    and legacy I/O space to root complexes and causing accesses to these
 *    regions to be routed to the correct RC (or another mechanism inside the
 *    processor such as the FCH or an RCiEP).
 *
 * The first two pieces of this are discussed further here; resource allocation
 * is discussed more generally in the next section and applies to both PCIe and
 * other protocols.  What is written here should be thought of as a model: a
 * useful simplification of reality.  AMD does not, generally, provide theory of
 * operation documentation for its non-architectural logic, which means that
 * what we have assembled here reflects an empirical understanding of the system
 * that may not match the underlying implementation in all respects.  Readers
 * with access to the PPRs will find references to named registers helpful
 * anchor points, but should be aware that this interpretation of how those
 * registers should be used or what they really do may not be entirely accurate.
 * This is best-effort documentation that should be improved as new information
 * becomes known.
 *
 * DXIO is the distributed crossbar I/O subsystem found in these SoCs.  This
 * term is used in several ways, referring both to the subsystem containing the
 * PCS, the muxes, and crossbars that implement this in hardware and to a
 * firmware application that we believe runs on MP1.  The latter is potentially
 * confusing because MP1 is also referred to as the SMU, but "SMU firmware" and
 * "DXIO firmware" are different pieces of code that perform different
 * functions.  Even more confusingly, both the SMU firmware and DXIO firmware
 * provide RPC interfaces, and the DXIO RPCs are accessed through a passthrough
 * SMU RPC function; see milan_dxio_rpc().  These form a critical mechanism for
 * accomplishing the first of our goals: the Link Initialisation State Machine
 * (LISM), a cooperative software-firmware subsystem that drives most low-level
 * PCIe core/port configuration.
 *
 * The LISM is a per-iodie linear state machine (so far as we know, there are no
 * backward transitions possible -- but we also know that handling errors is
 * extremely difficult).  The expected terminal state is that all ports that are
 * expected to exist, and their associated core and bridge logic, have been
 * constructed, configured, and if a downstream link partner is present and
 * working, the link has been negotiated and trained up.  Importantly, in AMD's
 * implementation, the entire LISM executes before any hotplug configuration is
 * done, meaning that the model at this stage is legacy non-hotpluggable static
 * link setup.  While it's possible to declare to the DXIO subsystem that a port
 * is hotplug-capable, this does not appear to have much effect on how DXIO
 * firmware operates, and there is no *standard* means of performing essential
 * actions like turning on a power controller.  Slots or bays that need bits
 * changed in their standard slot control registers for downstream devices to
 * link up -- or to have PERST released -- will fail to train at this stage and
 * the LISM will terminate with the corresponding ports in a failed state.
 * After configuring the hotplug firmware, those downstream devices can be
 * controlled and will (potentially) link up.  It is possible to integrate
 * hotplug firmware configuration into the LISM, which importantly allows
 * turning on power controllers, releasing PERST, and performing other actions
 * on any downstream devices attached to hotplug-capable ports at the normal
 * time during LISM execution; however, the current implementation does not do
 * so.  Unfortunately, some classes of failure during the link-training portion
 * of LISM execution result in DXIO firmware incorrectly changing PCIe port
 * registers in ways that prevent a working device from linking up properly upon
 * a subsequent hot-insertion.  This is one of several races inherent in this
 * mechanism; it's very likely that devices hot-inserted or hot-removed during
 * LISM execution will confuse the firmware as well.  An important area of
 * future work involves making sure that devices attached to all hotplug-capable
 * ports are powered off and held in reset until LISM execution has completely
 * finished, then overriding most of the firmware-created per-port link control
 * parameters prior to configuring hotplug and allowing those devices to be
 * turned on and come out of reset.  Doing so guarantees that when link training
 * begins, the port's link controller will be in the same known and expected
 * state it would be in when link training was first attempted (as if the port
 * were non-hotplug-capable).
 *
 * While there are many additional LISM states, there are really only three of
 * interest to us, plus a fourth pseudo-state.  Those states are:
 *
 * MAPPED - DXIO engine configuration (see milan_dxio_data.c) describing each
 * port to be created has been sent to DXIO firmware, accepted, and the
 * corresponding core and port setup completed so that port numbers are mapped
 * to specific hardware lanes and the corresponding PCIEPORT registers can be
 * used to control each port.  This is the first state reached after passing all
 * engine and other configuration parameters to DXIO firmware and starting the
 * LISM.
 *
 * CONFIGURED - Nominally, at this point all firmware-driven changes to core and
 * port registers has been completed, and upon resuming the LISM out of this
 * state link training will be attempted.  In reality, firmware does make
 * additional (undocumented, of course) changes after this state.  Perhaps more
 * significantly, once this state has been reached, firmware has latched the
 * "straps" into each PCIe core; more on this later.
 *
 * PERST - This is a pseudo-state.  After resuming the LISM out of the
 * CONFIGURED state, firmware will next signal not a new state but a request for
 * software to release PERST to all downstream devices attached through the I/O
 * die (for Milan, this means everything hanging off the socket for which this
 * LISM is being run; the LISM is run to completion for each socket in turn,
 * rather than advancing to each state on all sockets together).  The intent
 * here is that if PERST is driven by the PCIE_RST_L signals, sharing pins with
 * GPIOs, those pins can be controlled directly by software at this time.  One
 * would think that instead the PCIe core logic could do this itself, but there
 * appear to be timing considerations: leaving PERST deasserted "too long" may
 * cause training logic to give up and enter various error states, so this
 * mechanism allows software to ensure that PERST is released immediately before
 * link training will begin.  Critically, if one uses instead the PERST
 * mechanism intended for hotplug-capable devices in which PERST signals are
 * supplied by GPIO expanders under hotplug firmware control, that setup hasn't
 * been done at this point and there is no way to release PERST.  See notes
 * above on the relationship between the legacy one-shot PCIe LISM and the
 * hotplug subsystem.  In this case, downstream devices cannot be taken out of
 * reset and will not train during LISM execution.
 *
 * DONE - Upon resuming out of the PERST pseudo-state, firmware will release the
 * HOLD_TRAINING bit for each port, allowing the standard LTSSM to begin
 * executing.  After approximately 1 second, whether each port's link has
 * trained or not, we arrive at the DONE state.  At this point, we can retrieve
 * the DXIO firmware's understanding of each engine (port) configuration
 * including its training status.  We can also perform additional core and port
 * configuration, set up hotplug, and perform standard PCI device enumeration.
 *
 * LISM execution is started by software, which then polls firmware for notices
 * that we've advanced to the next state.  At each state execution then stops
 * until we deliberately resume it, which means that we have an opportunity to
 * do arbitrary work, including directly setting registers, setting "straps",
 * logging debug data, and more.
 *
 * -------------
 * PCIe "Straps"
 * -------------
 *
 * When one thinks of a strap, one normally imagines an input pin that is
 * externally tied to a specific voltage level or another pin via a precision
 * resistor, which in turns latches some documented behaviour when the device is
 * taken out of reset.  All of the "straps" we discuss in terms of PCIe (see
 * milan_fabric_init_pcie_straps()) are nothing like this.  First, all of the
 * NBIO logic is internal to the SoC; these settings do not have any external
 * pins which is certainly good because there are thousands of bits.  In
 * reality, these are just registers that are latched into other logic at one or
 * more defined (but undocumented!) points during LISM execution.  These come in
 * two different flavours, one for NBIFs and one for PCIe.  The registers
 * containing the strap fields for NBIFs are mostly documented in the PPR, but
 * their PCIe counterparts are not.  Our model, then, is this:
 *
 * 1. Writing to a PCIe strap really means writing to a hidden undocumented
 *    register through the RSMU associated with the PCIe core.
 *
 * 2. At some point in LISM execution, a subset of these registers are latched
 *    by DXIO firmware, probably by performing operations involved in taking the
 *    core out of reset (see PCIECORE::SWRST_xx registers).  There may be more
 *    than one such step, latching different subsets.  NOT ALL REGISTERS ARE
 *    LATCHED IN DURING LISM EXECUTION!  Some of these "straps" can be changed
 *    with immediate effect even after LISM execution has completed.  When they
 *    are latched, some fields end up directly in documented registers.  Others
 *    affect internal behaviour directly, and some are simply writable
 *    interfaces to otherwise read-only fields.  Importantly, some have elements
 *    of all of these.  The latching process may be done in hardware, may be
 *    done by the RSMU, or may be done by DXIO firmware simply copying data
 *    around.  We don't know, and in a sense it doesn't matter.
 *
 * 3. Firmware can and does write to these hidden strap registers itself,
 *    sometimes replacing software's values if the sequence isn't right.  Even
 *    more importantly, many of the documented register fields in which these
 *    values end up when latched are also writable by both software and
 *    firmware.  This means that a "strapped" value will replace the contents of
 *    the documented register that were constructed at POR or written
 *    previously.  It also means the converse: software -- and firmware! -- can
 *    directly change the contents of the documented register after the hidden
 *    strap register has been written and latched.
 *
 * Do not confuse these RSMU-accessed "strap" registers with documented
 * registers with STRAP in their names.  Often they are related, in that some of
 * the contents of hidden RSMU-accessed registers end up in the documented
 * registers by one means or another, but not always.  And the hidden "strap"
 * registers are in any case separate from the documented registers and have
 * different addressing, access mechanisms, and layouts.
 *
 * One of the most valuable improvements to our body of documentation here and
 * alongside register definitions is an inventory of when and how fields are
 * accessed.  That is: which of these registers/fields (in hidden strap
 * registers or documented ones) are modified by DXIO firmware, and if so, in
 * which LISM state(s)?
 *
 * -------------------
 * Resource Allocation
 * -------------------
 *
 * We route and allocate/reserve a variety of resources to either PCIe or
 * generic devices.  These include PCI bus numbers (PCIe only, obviously),
 * memory-mapped IO address spaces both above and below the 32-bit boundary, and
 * legacy I/O space ("ports" in x86 parlance).  Resources allocated to non-PCIe
 * devices are referred to as "gen" or generic; these resources are used by
 * peripherals inside the FCH as well as potentially by others that are neither
 * PCI-like nor part of the FCH; e.g., the PSP or SMU mailbox apertures which
 * can be assigned resources via BARs.  The Milan PPR 13.1.4.4 imposes certain
 * requirements on where this generic space is located and provides an
 * incomplete list of such consumers.  Note that the requirement that all
 * non-PCI resources of a particular type on an IOMS must be contiguous is
 * believed not to be a real requirement but rather an artefact of the way AMD's
 * firmware works; the true requirement is the one that's explicitly stated:
 * each IOMS's allocation of a resource type must be contiguous.  Nevertheless,
 * it's convenient to allocate each kind of consumer its own contiguous space as
 * this allows for allocations of the largest possible size by those consumers
 * (e.g., PCI bridges).
 *
 * On the fabric's primary IOMS (the IOMS on the primary IO die to which the FCH
 * is attached), we always reserve the compatibility legacy I/O and 32-bit MMIO
 * spaces for generic consumers on that IOMS.  These are:
 *
 * - ZEN_IOPORT_COMPAT_SIZE ports beginning at 0 for legacy I/O
 * - ZEN_COMPAT_MMIO_SIZE bytes beginning at ZEN_PHYSADDR_COMPAT_MMIO for
 * 32-bit MMIO
 *
 * These reservations are unconditional for the primary IOMS; they are intended
 * mainly for accessing peripherals in the primary FCH that are located at fixed
 * addresses, including the ixbar at fixed legacy I/O ports.
 *
 * Currently the size of the generic-device reservation of each type of resource
 * on secondary IOMSs (those that do not have the FCH attached and/or are not on
 * the primary IO die) is governed by fixed compile-time constants:
 *
 * MILAN_SEC_IOMS_GEN_IO_SPACE is the number of contiguous legacy I/O ports to
 * reserve for non-PCI consumers.  While not currently used, the remote FCH has
 * a unit called the A-Link/B-Link bridge accessed via legacy I/O space at a
 * group of ports programmable via an FCH BAR; to access this, we would need to
 * reserve space routed to the secondary FCH's IOMS, so we try to do that.
 *
 * MILAN_SEC_IOMS_GEN_MMIO32_SPACE is the size in bytes of the contiguous MMIO
 * region below the 32-bit boundary to reserve for non-PCI consumers.
 *
 * MILAN_SEC_IOMS_GEN_MMIO64_SPACE is the corresponding figure for MMIO space
 * above the 32-bit boundary.
 *
 * These will be reduced (possibly resulting in FCH peripherals not working) if
 * the amount of space specified by the corresponding macro would be half or
 * more of the total resources routed to the IOMS; that is, we prioritise PCIe,
 * as other than the FCH we do not currently use any of the generic devices.
 *
 * These allocations/reservations do not affect routing so the division between
 * PCI and generic for a given IOMS does not have to be expressed in terms of DF
 * granularity.  It's unclear whether this should be tunable at runtime, or
 * whether we want to be more clever by allowing it to be dynamic and altering
 * the routing tables at runtime.  Either would be challenging, and can
 * undoubtedly wait until we have a real need for any of this.  See
 * milan_xx_allocate() for the implementation of these allocations/reservations.
 *
 * The last thing to be aware of here is what happens before we set up legacy
 * I/O space and MMIO routing.  Here the implementation helps us out
 * considerably: both legacy I/O space and MMIO are routed into the subtractive
 * (compatibility) space.  This is a fancy way of saying the FCH in socket 0 is
 * given an opportunity to decode them.  If it doesn't, reads return all-1s and
 * writes are ignored.  We make use of this property in a number of ways, not
 * least that the earlyboot code can make use of UARTs and GPIOs.  Additionally,
 * we rely on this for setting up spread-spectrum clocking via the FCH prior to
 * running any of this code; that allows us to calibrate the TSC properly before
 * we get here and therefore to rely on having drv_usecwait(), as well as making
 * sure SSC is on before we start doing any PCIe link training that would
 * otherwise generate noise.
 *
 * -----------
 * Future Work
 * -----------
 *
 * Most of the PCIe parts of this could be separated out of this file.  The NBIO
 * device (root complex) could be used as the attachment point for the npe(7d)
 * driver instead of the pseudo-nexus constructed today.  We could use NDI
 * interfaces for much of the resource allocation done here, especially if the
 * DF is also represented in the devinfo tree with appopriate drivers.
 *
 * "Generic" PCIe resource allocation via pcie_autoconfig is a good fit for
 * enumeration and allocation for non-hotplug-capable systems with PC firmware.
 * It's not a good fit for machines without firmware, and it's especially poor
 * on machines with hotplug-capable attachment points.  A larger-scale (not
 * limited to this kernel architecture) change here would be to treat all PCIe
 * devices as being attached in a hotplug-capable manner, and simply treat
 * non-hotplug-capable devices that are present at boot as if they had been
 * hot-inserted during boot.
 *
 * PCIe port numbering and mapping is currently static, with fixed values in the
 * engine configuration.  This could instead by dynamic.  Bus ranges are also
 * allocated to bridges in a static and inflexible manner that does not properly
 * support additional bridges or switches below the host bridge.
 *
 * There are numerous other opportunities to improve aspects of this software
 * noted inline with XXX.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/ksynch.h>
#include <sys/pci.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/pci_ident.h>
#include <sys/pcie.h>
#include <sys/spl.h>
#include <sys/debug.h>
#include <sys/prom_debug.h>
#include <sys/platform_detect.h>
#include <sys/x86_archext.h>
#include <sys/bitext.h>
#include <sys/sysmacros.h>
#include <sys/memlist_impl.h>
#include <sys/machsystm.h>
#include <sys/plat/pci_prd.h>
#include <sys/apic.h>
#include <sys/cpuvar.h>
#include <sys/apob.h>
#include <sys/kapob.h>
#include <sys/amdzen/ccd.h>
#include <sys/amdzen/fch.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/fch/smi.h>
#include <sys/io/milan/fabric_impl.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/hacks.h>
#include <sys/io/milan/ioapic.h>
#include <sys/io/milan/iohc.h>
#include <sys/io/milan/iommu.h>
#include <sys/io/milan/nbif_impl.h>
#include <sys/io/milan/pcie.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/io/milan/pcie_rsmu.h>
#include <sys/io/milan/smu_impl.h>

#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/dxio_data.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>
#include <sys/io/zen/smu_impl.h>

#include <asm/bitmap.h>

#include <sys/amdzen/df.h>

#include <milan/milan_apob.h>

/*
 * XXX This header contains a lot of the definitions that the broader system is
 * currently using for register definitions. For the moment we're trying to keep
 * this consolidated, hence this wacky include path.
 */
#include <io/amdzen/amdzen.h>

/*
 * This is a structure that we can use internally to pass around a DXIO RPC
 * request.
 */
typedef struct milan_dxio_rpc {
	uint32_t		mdr_req;
	uint32_t		mdr_dxio_resp;
	zen_smu_rpc_res_t	mdr_smu_resp;
	uint32_t		mdr_engine;
	uint32_t		mdr_arg0;
	uint32_t		mdr_arg1;
	uint32_t		mdr_arg2;
	uint32_t		mdr_arg3;
} milan_dxio_rpc_t;

/*
 * These three tables encode knowledge about how the SoC assigns devices and
 * functions to root ports.
 */
static const zen_pcie_port_info_t
    milan_pcie[MILAN_IOMS_MAX_PCIE_CORES][MILAN_PCIE_CORE_MAX_PORTS] = {
	[0] = {
		{ .zppi_dev = 0x1, .zppi_func = 0x1 },
		{ .zppi_dev = 0x1, .zppi_func = 0x2 },
		{ .zppi_dev = 0x1, .zppi_func = 0x3 },
		{ .zppi_dev = 0x1, .zppi_func = 0x4 },
		{ .zppi_dev = 0x1, .zppi_func = 0x5 },
		{ .zppi_dev = 0x1, .zppi_func = 0x6 },
		{ .zppi_dev = 0x1, .zppi_func = 0x7 },
		{ .zppi_dev = 0x2, .zppi_func = 0x1 }
	},
	[1] = {
		{ .zppi_dev = 0x3, .zppi_func = 0x1 },
		{ .zppi_dev = 0x3, .zppi_func = 0x2 },
		{ .zppi_dev = 0x3, .zppi_func = 0x3 },
		{ .zppi_dev = 0x3, .zppi_func = 0x4 },
		{ .zppi_dev = 0x3, .zppi_func = 0x5 },
		{ .zppi_dev = 0x3, .zppi_func = 0x6 },
		{ .zppi_dev = 0x3, .zppi_func = 0x7 },
		{ .zppi_dev = 0x4, .zppi_func = 0x1 }
	},
	[2] = {
		{ .zppi_dev = 0x5, .zppi_func = 0x1 },
		{ .zppi_dev = 0x5, .zppi_func = 0x2 }
	}
};

/*
 * These are internal bridges that correspond to NBIFs; they are modeled as
 * ports but there is no physical port brought out of the package.
 */
static const zen_pcie_port_info_t milan_int_ports[4] = {
	{ 0x7, 0x1 },
	{ 0x8, 0x1 },
	{ 0x8, 0x2 },
	{ 0x8, 0x3 }
};

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently follow the recommendation of the PPR.
 */
static const zen_ioapic_info_t milan_ioapic_routes[IOAPIC_NROUTES] = {
	[0] = { .zii_group = 0x0, .zii_map = 0x10,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[1] = { .zii_group = 0x1, .zii_map = 0x11,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[2] = { .zii_group = 0x2, .zii_map = 0x12,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[3] = { .zii_group = 0x3, .zii_map = 0x13,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[4] = { .zii_group = 0x4, .zii_map = 0x10,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[5] = { .zii_group = 0x5, .zii_map = 0x11,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[6] = { .zii_group = 0x6, .zii_map = 0x12,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[7] = { .zii_group = 0x7, .zii_map = 0x13,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[8] = { .zii_group = 0x7, .zii_map = 0x0c,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[9] = { .zii_group = 0x6, .zii_map = 0x0d,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[10] = { .zii_group = 0x5, .zii_map = 0x0e,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[11] = { .zii_group = 0x4, .zii_map = 0x0f,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[12] = { .zii_group = 0x3, .zii_map = 0x0c,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[13] = { .zii_group = 0x2, .zii_map = 0x0d,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[14] = { .zii_group = 0x1, .zii_map = 0x0e,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[15] = { .zii_group = 0x0, .zii_map = 0x0f,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[16] = { .zii_group = 0x0, .zii_map = 0x08,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[17] = { .zii_group = 0x1, .zii_map = 0x09,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[18] = { .zii_group = 0x2, .zii_map = 0x0a,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[19] = { .zii_group = 0x3, .zii_map = 0x0b,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[20] = { .zii_group = 0x4, .zii_map = 0x08,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[21] = { .zii_group = 0x5, .zii_map = 0x09,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC }
};

CTASSERT(ARRAY_SIZE(milan_ioapic_routes) == IOAPIC_NROUTES);

const uint8_t milan_nbif_nfunc[] = {
	[0] = MILAN_NBIF0_NFUNCS,
	[1] = MILAN_NBIF1_NFUNCS,
	[2] = MILAN_NBIF2_NFUNCS
};

const zen_nbif_info_t milan_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS] = {
	[0] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_NTB, .zni_dev = 0, .zni_func = 1 },
		{ .zni_type = ZEN_NBIF_T_PTDMA, .zni_dev = 0, .zni_func = 2 }
	},
	[1] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_PSPCCP, .zni_dev = 0, .zni_func = 1 },
		{ .zni_type = ZEN_NBIF_T_PTDMA, .zni_dev = 0, .zni_func = 2 },
		{ .zni_type = ZEN_NBIF_T_USB, .zni_dev = 0, .zni_func = 3 },
		{ .zni_type = ZEN_NBIF_T_AZ, .zni_dev = 0, .zni_func = 4 },
		{ .zni_type = ZEN_NBIF_T_SATA, .zni_dev = 1, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_SATA, .zni_dev = 2, .zni_func = 0 }
	},
	[2] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_NTB, .zni_dev = 0, .zni_func = 1 },
		{ .zni_type = ZEN_NBIF_T_NVME, .zni_dev = 0, .zni_func = 2 }
	}
};

/*
 * This table encodes the mapping of the set of dxio lanes to a given PCIe core
 * on an IOMS. This is ordered such that all of the normal engines are present;
 * however, the wafl core, being special is not here. The dxio engine uses
 * different lane numbers than the phys. Note, that all lanes here are
 * inclusive. e.g. [start, end].
 */
static const zen_pcie_core_info_t milan_lane_maps[8] = {
	/* name, DXIO start, DXIO end, PHY start, PHY end */
	{ "G0", 0x10, 0x1f, 0x10, 0x1f },
	{ "P0", 0x2a, 0x39, 0x00, 0x0f },
	{ "P1", 0x3a, 0x49, 0x20, 0x2f },
	{ "G1", 0x00, 0x0f, 0x30, 0x3f },
	{ "G3", 0x72, 0x81, 0x60, 0x6f },
	{ "P3", 0x5a, 0x69, 0x70, 0x7f },
	{ "P2", 0x4a, 0x59, 0x50, 0x5f },
	{ "G2", 0x82, 0x91, 0x40, 0x4f }
};

static const zen_pcie_core_info_t milan_bonus_map = {
	"WAFL", 0x24, 0x25, 0x80, 0x81
};

/*
 * How many PCIe cores does this IOMS instance have?
 */
uint8_t
milan_ioms_n_pcie_cores(const uint8_t iomsno)
{
	if (iomsno == MILAN_NBIO_BONUS_IOMS)
		return (MILAN_IOMS_MAX_PCIE_CORES);
	return (MILAN_IOMS_MAX_PCIE_CORES - 1);
}

/*
 * How many PCIe ports does this core instance have?  Not all ports are
 * necessarily enabled, and ports that are disabled may have their associated
 * bridges hidden; this is used to compute the locations of register blocks that
 * pertain to the port that may exist.
 */
uint8_t
milan_pcie_core_n_ports(const uint8_t pcno)
{
	if (pcno == MILAN_IOMS_BONUS_PCIE_CORENO)
		return (MILAN_PCIE_CORE_WAFL_NPORTS);
	return (MILAN_PCIE_CORE_MAX_PORTS);
}

const zen_pcie_core_info_t *
milan_pcie_core_info(const uint8_t iomsno, const uint8_t coreno)
{
	uint8_t index;

	if (coreno == MILAN_IOMS_BONUS_PCIE_CORENO)
		return (&milan_bonus_map);

	index = iomsno * 2 + coreno;
	VERIFY3U(index, <, ARRAY_SIZE(milan_lane_maps));
	return (&milan_lane_maps[index]);
}

const zen_pcie_port_info_t *
milan_pcie_port_info(const uint8_t coreno, const uint8_t portno)
{
	return (&milan_pcie[coreno][portno]);
}

typedef enum milan_iommul1_subunit {
	MIL1SU_NBIF,
	MIL1SU_IOAGR
} milan_iommul1_subunit_t;

/*
 * XXX Belongs in a header.
 */
extern void *contig_alloc(size_t, ddi_dma_attr_t *, uintptr_t, int);
extern void contig_free(void *, size_t);

/*
 * Our primary global data. This is the reason that we exist.
 */
static milan_fabric_t milan_fabric;

void
milan_fabric_thread_get_dpm_weights(const zen_thread_t *thread,
    const uint64_t **wp, uint32_t *nentp)
{
	zen_ccd_t *ccd = thread->zt_core->zc_ccx->zcx_ccd;
	zen_iodie_t *ziodie = ccd->zcd_iodie;
	milan_iodie_t *iodie = ziodie->zi_uarch_iodie;
	*wp = iodie->mi_dpm_weights;
	*nentp = MILAN_MAX_DPM_WEIGHTS;
}

/*
 * Convenience functions for accessing SMN registers pertaining to a bridge.
 * These are candidates for making public if/when other code needs to manipulate
 * bridges.  There are some tradeoffs here: we don't need any of these
 * functions; callers could instead look up registers themselves, retrieve the
 * iodie by chasing back-pointers, and call zen_smn_{read,write}32()
 * themselves.  Indeed, they still can, and if there are many register accesses
 * to be made in code that materially affects performance, that is likely to be
 * preferable.  However, it has a major drawback: it requires each caller to get
 * the ordered set of instance numbers correct when constructing the register,
 * and there is little or nothing that can be done to help them.  Most of the
 * register accessors will blow up if the instance numbers are obviously out of
 * range, but there is little we can do to prevent them being given out of
 * order, for example.  Constructing incompatible struct types for each instance
 * level seems impractical.  So instead we isolate those calculations here and
 * allow callers to treat each bridge's (or other object's) collections of
 * pertinent registers opaquely.  This is probably closest to what we
 * conceptually want this to look like anyway; callers should be focused on
 * controlling the device, not on the mechanics of how to do so.  Nevertheless,
 * we do not foreclose on arbitrary SMN access if that's useful.
 *
 * We provide similar collections of functions below for other entities we
 * model in the fabric.
 */

smn_reg_t
milan_pcie_port_reg(const zen_pcie_port_t *const port,
    const smn_reg_def_t def)
{
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_IOHCDEV_PCIE:
		reg = milan_iohcdev_pcie_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	case SMN_UNIT_PCIE_PORT:
		reg = milan_pcie_port_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe port",
		    def.srd_unit);
	}

	return (reg);
}

smn_reg_t
milan_pcie_core_reg(const zen_pcie_core_t *const pc, const smn_reg_def_t def)
{
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_CORE:
		reg = milan_pcie_core_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno);
		break;
	case SMN_UNIT_PCIE_RSMU:
		reg = milan_pcie_rsmu_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno);
		break;
	case SMN_UNIT_IOMMUL1:
		reg = milan_iommul1_pcie_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe RC",
		    def.srd_unit);
	}

	return (reg);
}

/*
 * We consider the IOAGR to be part of the NBIO/IOHC/IOMS, so the IOMMUL1's
 * IOAGR block falls under the IOMS; the IOAPIC, SDPMUX, and IOMMUL2 are similar
 * as they do not (currently) have independent representation in the fabric.
 */
static smn_reg_t
milan_ioms_reg(const zen_ioms_t *const ioms, const smn_reg_def_t def,
    const uint16_t reginst)
{
	smn_reg_t reg;
	switch (def.srd_unit) {
	case SMN_UNIT_IOAPIC:
		reg = milan_ioapic_smn_reg(ioms->zio_num, def, reginst);
		break;
	case SMN_UNIT_IOHC:
		reg = milan_iohc_smn_reg(ioms->zio_num, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = milan_ioagr_smn_reg(ioms->zio_num, def, reginst);
		break;
	case SMN_UNIT_SDPMUX:
		reg = milan_sdpmux_smn_reg(ioms->zio_num, def, reginst);
		break;
	case SMN_UNIT_IOMMUL1: {
		/*
		 * Confusingly, this pertains to the IOMS, not the NBIF; there
		 * is only one unit per IOMS, not one per NBIF.  Because.  To
		 * accommodate this, we need to treat the reginst as an
		 * enumerated type to distinguish the sub-units.  As gross as
		 * this is, it greatly reduces triplication of register
		 * definitions.  There is no way to win here.
		 */
		const milan_iommul1_subunit_t su =
		    (const milan_iommul1_subunit_t)reginst;
		switch (su) {
		case MIL1SU_NBIF:
			reg = milan_iommul1_nbif_smn_reg(ioms->zio_num, def, 0);
			break;
		case MIL1SU_IOAGR:
			reg = milan_iommul1_ioagr_smn_reg(ioms->zio_num, def,
			    0);
			break;
		default:
			cmn_err(CE_PANIC, "invalid IOMMUL1 subunit %d", su);
			break;
		}
		break;
	}
	case SMN_UNIT_IOMMUL2:
		reg = milan_iommul2_smn_reg(ioms->zio_num, def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
		    def.srd_unit);
	}
	return (reg);
}

static smn_reg_t
milan_nbif_reg(const zen_nbif_t *const nbif, const smn_reg_def_t def,
    const uint16_t reginst)
{
	zen_ioms_t *ioms = nbif->zn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF:
		reg = milan_nbif_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, reginst);
		break;
	case SMN_UNIT_NBIF_ALT:
		reg = milan_nbif_alt_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF",
		    def.srd_unit);
	}

	return (reg);
}

static smn_reg_t
milan_nbif_func_reg(const zen_nbif_func_t *const func, const smn_reg_def_t def)
{
	zen_nbif_t *nbif = func->znf_nbif;
	zen_ioms_t *ioms = nbif->zn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF_FUNC:
		reg = milan_nbif_func_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, func->znf_dev, func->znf_func);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF func",
		    def.srd_unit);
	}

	return (reg);
}

smn_reg_t
milan_iodie_reg(const smn_reg_def_t def, const uint16_t reginst)
{
	smn_reg_t reg;
	switch (def.srd_unit) {
	case SMN_UNIT_SMU_RPC:
		reg = zen_smu_smn_reg(0, def, reginst);
		break;
	case SMN_UNIT_FCH_SMI:
		reg = fch_smi_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_PMIO:
		reg = fch_pmio_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_MISC_A:
		reg = fch_misc_a_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_I2CPAD:
		reg = fch_i2cpad_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_MISC_B:
		reg = fch_misc_b_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_I2C:
		reg = huashan_i2c_smn_reg(reginst, def);
		break;
	case SMN_UNIT_FCH_IOMUX:
		reg = fch_iomux_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_GPIO:
		reg = fch_gpio_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_RMTGPIO:
		reg = fch_rmtgpio_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_RMTMUX:
		reg = fch_rmtmux_smn_reg(def, reginst);
		break;
	case SMN_UNIT_FCH_RMTGPIO_AGG:
		reg = fch_rmtgpio_agg_smn_reg(def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IO die",
		    def.srd_unit);
	}
	return (reg);
}

void
milan_iohc_enable_nmi(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t v;

	/*
	 * On reset, the NMI destination in IOHC::IOHC_INTR_CNTL is set to
	 * 0xff.  We (emphatically) do not want any AP to get an NMI when we
	 * first power it on, so we deliberately set all NMI destinations to
	 * be the BSP.  Note that we do will not change this, even after APs
	 * are up (that is, NMIs will always go to the BSP):  changing it has
	 * non-zero runtime risk (see the comment above our actual enabling
	 * of NMI, below) and does not provide any value for our use case of
	 * NMI.
	 */
	reg = milan_ioms_reg(ioms, D_IOHC_INTR_CTL, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_INTR_CTL_SET_NMI_DEST_CTL(v, 0);
	zen_ioms_write(ioms, reg, v);

	if ((zen_ioms_flags(ioms) & ZEN_IOMS_F_HAS_FCH) != 0) {
		reg = milan_ioms_reg(ioms, D_IOHC_PIN_CTL, 0);
		v = zen_ioms_read(ioms, reg);
		v = IOHC_PIN_CTL_SET_MODE_NMI(v);
		zen_ioms_write(ioms, reg, v);
	}

	/*
	 * Once we enable this, we can immediately take an NMI if it's
	 * currently asserted.  We want to do this last and clear out of here
	 * as quickly as possible:  this is all a bit dodgy, but the NMI
	 * handler itself needs to issue an SMN write to indicate EOI -- and
	 * if it finds that SMN-related locks are held, we will panic.  To
	 * reduce the likelihood of that, we are going to enable NMI and
	 * skedaddle...
	 */
	reg = milan_ioms_reg(ioms, D_IOHC_MISC_RAS_CTL, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_MISC_RAS_CTL_SET_NMI_SYNCFLOOD_EN(v, 1);
	zen_ioms_write(ioms, reg, v);
}

void
milan_iohc_nmi_eoi(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t v;

	reg = milan_ioms_reg(ioms, D_IOHC_FCTL2, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_FCTL2_GET_NMI(v);
	if (v != 0) {
		/*
		 * We have no ability to handle the other bits here, as
		 * those conditions may not have resulted in an NMI.  Clear only
		 * the bit whose condition we have handled.
		 */
		zen_ioms_write(ioms, reg, v);
		reg = milan_ioms_reg(ioms, D_IOHC_INTR_EOI, 0);
		v = IOHC_INTR_EOI_SET_NMI(0);
		zen_ioms_write(ioms, reg, v);
	}
}

void
milan_pcie_dbg_signal(void)
{
	static bool gpio_configured;

	/*
	 * On Gimlet, we want to signal via GPIO that we're collecting register
	 * data.  While rev C boards have a number of accessible GPIOs -- though
	 * intended for other uses -- rev B boards do not.  The only one that's
	 * available on all rev B and C boards is AGPIO129, which is shared with
	 * KBRST_L.  Nothing uses this GPIO at all, nor any of the other
	 * functions associated with the pin, but it has a handy test point.  We
	 * will toggle this pin's state each time we collect registers.  This
	 * allows someone using a logic analyser to look at low-speed signals to
	 * correlate those observations with these register values.  The
	 * register values are not a snapshot, but we do collect the timestamp
	 * associated with each one so it's at least possible to reassemble a
	 * complete strip chart with coordinated timestamps.
	 *
	 * If this is the first time we're using the GPIO, we will reset its
	 * output, then toggle it twice at 1 microsecond intervals to provide a
	 * clear start time (since the GPIO was previously an input and would
	 * have read at an undefined level).
	 */
	if (oxide_board_data->obd_board == OXIDE_BOARD_GIMLET) {
		if (!gpio_configured) {
			milan_hack_gpio(ZHGOP_CONFIGURE, 129);
			milan_hack_gpio(ZHGOP_TOGGLE, 129);
			drv_usecwait(1);
			gpio_configured = true;
		}
		milan_hack_gpio(ZHGOP_TOGGLE, 129);
	}
}

static bool
milan_smu_rpc_i2c_switch(zen_iodie_t *iodie, uint32_t addr)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	rpc.zsr_req = MILAN_SMU_OP_I2C_SWITCH_ADDR;
	rpc.zsr_args[0] = addr;
	res = zen_smu_rpc(iodie, &rpc);

	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Set i2c address RPC Failed for Address "
		    "0x%x: %s (SMU 0x%x)", addr, zen_smu_rpc_res_str(res),
		    rpc.zsr_resp);
	}

	return (res == ZEN_SMU_RPC_OK);
}

static bool
milan_smu_rpc_give_address(zen_iodie_t *iodie, milan_smu_addr_kind_t kind,
    uint64_t addr)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	switch (kind) {
	case MSAK_GENERIC:
		rpc.zsr_req = MILAN_SMU_OP_HAVE_AN_ADDRESS;
		break;
	case MSAK_HOTPLUG:
		/*
		 * For a long time, hotplug table addresses were provided to the
		 * SMU in the same manner as any others; however, in recent
		 * versions there is a separate RPC for that.
		 */
		rpc.zsr_req = zen_smu_version_at_least(iodie, 45, 90, 0) ?
		    MILAN_SMU_OP_HAVE_A_HP_ADDRESS :
		    MILAN_SMU_OP_HAVE_AN_ADDRESS;
		break;
	default:
		panic("invalid SMU address kind %d", (int)kind);
	}
	rpc.zsr_args[0] = bitx64(addr, 31, 0);
	rpc.zsr_args[1] = bitx64(addr, 63, 32);

	res = zen_smu_rpc(iodie, &rpc);
	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Have an Address RPC Failed: addr: 0x%lx, "
		    "SMU req 0x%x resp %s (SMU 0x%x)", addr, rpc.zsr_req,
		    zen_smu_rpc_res_str(res), rpc.zsr_resp);
	}

	return (res == ZEN_SMU_RPC_OK);

}

static bool
milan_smu_rpc_send_hotplug_table(zen_iodie_t *iodie)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	rpc.zsr_req = MILAN_SMU_OP_TX_PCIE_HP_TABLE;
	res = zen_smu_rpc(iodie, &rpc);
	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU TX Hotplug Table Failed: %s (SMU 0x%x)",
		    zen_smu_rpc_res_str(res), rpc.zsr_resp);
	}

	return (res == ZEN_SMU_RPC_OK);
}

static bool
milan_smu_rpc_hotplug_flags(zen_iodie_t *iodie, uint32_t flags)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	rpc.zsr_req = MILAN_SMU_OP_SET_HOPTLUG_FLAGS;
	rpc.zsr_args[0] = flags;
	res = zen_smu_rpc(iodie, &rpc);
	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Set Hotplug Flags failed: %s (SMU 0x%x)",
		    zen_smu_rpc_res_str(res), rpc.zsr_resp);
	}

	return (res == ZEN_SMU_RPC_OK);
}
static bool
milan_smu_rpc_start_hotplug(zen_iodie_t *iodie, bool one_based, uint8_t flags)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	rpc.zsr_req = MILAN_SMU_OP_START_HOTPLUG;
	if (one_based) {
		rpc.zsr_args[0] = 1;
	}
	rpc.zsr_args[0] |= flags;
	res = zen_smu_rpc(iodie, &rpc);
	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Start Hotplug Failed: %s (SMU 0x%x)",
		    zen_smu_rpc_res_str(res), rpc.zsr_resp);
	}

	return (res == ZEN_SMU_RPC_OK);
}

static bool
milan_smu_rpc_read_dpm_weights(zen_iodie_t *iodie, uint64_t *buf, size_t len)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	len = MIN(len, MILAN_MAX_DPM_WEIGHTS * sizeof (uint64_t));
	bzero(buf, len);
	rpc.zsr_req = MILAN_SMU_OP_READ_DPM_WEIGHT;

	for (size_t idx = 0; idx < len / sizeof (uint64_t); idx++) {
		rpc.zsr_args[0] = (uint32_t)idx;
		res = zen_smu_rpc(iodie, &rpc);
		if (res != ZEN_SMU_RPC_OK) {
			cmn_err(CE_WARN, "SMU Read DPM Weights Failed: %s "
			    "(index %lu, SMU 0x%x)", zen_smu_rpc_res_str(res),
			    idx, rpc.zsr_resp);
			return (false);
		}

		buf[idx] = rpc.zsr_args[1];
		buf[idx] <<= 32;
		buf[idx] |= rpc.zsr_args[0];
	}

	return (true);
}

static bool
milan_dxio_version_at_least(const zen_iodie_t *iodie,
    const uint32_t major, const uint32_t minor)
{
	return (iodie->zi_dxio_fw[0] > major ||
	    (iodie->zi_dxio_fw[0] == major && iodie->zi_dxio_fw[1] >= minor));
}

static bool
milan_dxio_rpc(zen_iodie_t *iodie, milan_dxio_rpc_t *dxio_rpc)
{
	zen_smu_rpc_t smu_rpc = { 0 };
	zen_smu_rpc_res_t res;

	smu_rpc.zsr_req = MILAN_SMU_OP_DXIO;
	smu_rpc.zsr_args[0] = dxio_rpc->mdr_req;
	smu_rpc.zsr_args[1] = dxio_rpc->mdr_engine;
	smu_rpc.zsr_args[2] = dxio_rpc->mdr_arg0;
	smu_rpc.zsr_args[3] = dxio_rpc->mdr_arg1;
	smu_rpc.zsr_args[4] = dxio_rpc->mdr_arg2;
	smu_rpc.zsr_args[5] = dxio_rpc->mdr_arg3;

	res = zen_smu_rpc(iodie, &smu_rpc);
	dxio_rpc->mdr_smu_resp = res;
	if (res == ZEN_SMU_RPC_OK) {
		dxio_rpc->mdr_dxio_resp = smu_rpc.zsr_args[0];
		dxio_rpc->mdr_engine = smu_rpc.zsr_args[1];
		dxio_rpc->mdr_arg0 = smu_rpc.zsr_args[2];
		dxio_rpc->mdr_arg1 = smu_rpc.zsr_args[3];
		dxio_rpc->mdr_arg2 = smu_rpc.zsr_args[4];
		dxio_rpc->mdr_arg3 = smu_rpc.zsr_args[5];
	}

	return (res == ZEN_SMU_RPC_OK &&
	    dxio_rpc->mdr_dxio_resp == MILAN_DXIO_RPC_OK);
}

static bool
milan_dxio_rpc_init(zen_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_INIT;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Init RPC Failed: SMU %s, DXIO: 0x%x",
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_set_var(zen_iodie_t *iodie, uint32_t var, uint32_t val)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_VARIABLE;
	rpc.mdr_engine = var;
	rpc.mdr_arg0 = val;

	(void) milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != ZEN_SMU_RPC_OK ||
	    (rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK &&
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_MBOX_IDLE)) {
		cmn_err(CE_WARN, "DXIO Set Variable Failed: Var: 0x%x, "
		    "Val: 0x%x, SMU %s, DXIO: 0x%x", var, val,
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_pcie_poweroff_config(zen_iodie_t *iodie, uint8_t delay,
    bool disable_prep)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_VARIABLE;
	rpc.mdr_engine = MILAN_DXIO_VAR_PCIE_POWER_OFF_DELAY;
	rpc.mdr_arg0 = delay;
	rpc.mdr_arg1 = disable_prep ? 1 : 0;

	(void) milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != ZEN_SMU_RPC_OK ||
	    (rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK &&
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_MBOX_IDLE)) {
		cmn_err(CE_WARN, "DXIO Set PCIe Power Off Config Failed: "
		    "Delay: 0x%x, Disable Prep: 0x%x, SMU %s, DXIO: 0x%x",
		    delay, disable_prep, zen_smu_rpc_res_str(rpc.mdr_smu_resp),
		    rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_clock_gating(zen_iodie_t *iodie, uint8_t mask, uint8_t val)
{
	milan_dxio_rpc_t rpc = { 0 };

	/*
	 * The mask and val are only allowed to be 7-bit values.
	 */
	VERIFY0(mask & 0x80);
	VERIFY0(val & 0x80);
	rpc.mdr_req = MILAN_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = MILAN_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = MILAN_DXIO_RT_CONF_CLOCK_GATE;
	rpc.mdr_arg1 = mask;
	rpc.mdr_arg2 = val;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Clock Gating Failed: SMU %s, "
		    "DXIO: 0x%x", zen_smu_rpc_res_str(rpc.mdr_smu_resp),
		    rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

/*
 * Currently there are no capabilities defined, which makes it hard for us to
 * know the exact command layout here. The only thing we know is safe is that
 * it's all zeros, though it probably otherwise will look like
 * MILAN_DXIO_OP_LOAD_DATA.
 */
static bool
milan_dxio_rpc_load_caps(zen_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_LOAD_CAPS;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Load Caps Failed: SMU %s, DXIO: 0x%x",
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_load_data(zen_iodie_t *iodie, uint32_t type,
    uint64_t phys_addr, uint32_t len, uint32_t mystery)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_LOAD_DATA;
	rpc.mdr_engine = (uint32_t)(phys_addr >> 32);
	rpc.mdr_arg0 = phys_addr & 0xffffffff;
	rpc.mdr_arg1 = len / 4;
	rpc.mdr_arg2 = mystery;
	rpc.mdr_arg3 = type;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Load Data Failed: Heap: 0x%x, PA: "
		    "0x%lx, Len: 0x%x, SMU %s, DXIO: 0x%x", type, phys_addr,
		    len, zen_smu_rpc_res_str(rpc.mdr_smu_resp),
		    rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_conf_training(zen_iodie_t *iodie, uint32_t reset_time,
    uint32_t rx_poll, uint32_t l0_poll)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = MILAN_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = MILAN_DXIO_RT_CONF_PCIE_TRAIN;
	rpc.mdr_arg1 = reset_time;
	rpc.mdr_arg2 = rx_poll;
	rpc.mdr_arg3 = l0_poll;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Conf. PCIe Training RPC Failed: "
		    "SMU %s, DXIO: 0x%x", zen_smu_rpc_res_str(rpc.mdr_smu_resp),
		    rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

/*
 * This is a hodgepodge RPC that is used to set various rt configuration
 * properties.
 */
static bool
milan_dxio_rpc_misc_rt_conf(zen_iodie_t *iodie, uint32_t code,
    bool state)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = MILAN_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = MILAN_DXIO_RT_SET_CONF;
	rpc.mdr_arg1 = code;
	rpc.mdr_arg2 = state ? 1 : 0;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Set Misc. rt conf failed: Code: 0x%x, "
		    "Val: 0x%x, SMU %s, DXIO: 0x%x", code, state,
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_sm_start(zen_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_START_SM;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU %s, "
		    "DXIO: 0x%x",
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_sm_resume(zen_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_RESUME_SM;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU %s, "
		    "DXIO: 0x%x",
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
milan_dxio_rpc_sm_reload(zen_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_RELOAD_SM;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO SM Reload RPC Failed: SMU %s, "
		    "DXIO: 0x%x",
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}


static bool
milan_dxio_rpc_sm_getstate(zen_iodie_t *iodie, milan_dxio_reply_t *smp)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_GET_SM_STATE;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU %s, DXIO: 0x%x",
		    zen_smu_rpc_res_str(rpc.mdr_smu_resp), rpc.mdr_dxio_resp);
		return (false);
	}

	smp->mds_type = bitx64(rpc.mdr_engine, 7, 0);
	smp->mds_nargs = bitx64(rpc.mdr_engine, 15, 8);
	smp->mds_arg0 = rpc.mdr_arg0;
	smp->mds_arg1 = rpc.mdr_arg1;
	smp->mds_arg2 = rpc.mdr_arg2;
	smp->mds_arg3 = rpc.mdr_arg3;

	return (true);
}

/*
 * Retrieve the current engine data from DXIO.
 */
static bool
milan_dxio_rpc_retrieve_engine(zen_iodie_t *iodie)
{
	zen_dxio_config_t *conf = &iodie->zi_dxio_conf;
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_GET_ENGINE_CFG;
	rpc.mdr_engine = (uint32_t)(conf->zdc_pa >> 32);
	rpc.mdr_arg0 = conf->zdc_pa & 0xffffffff;
	rpc.mdr_arg1 = conf->zdc_alloc_len / 4;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Retrieve Engine Failed: SMU %s, "
		    "DXIO: 0x%x", zen_smu_rpc_res_str(rpc.mdr_smu_resp),
		    rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

bool
milan_get_dxio_fw_version(zen_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_GET_VERSION;

	if (!milan_dxio_rpc(iodie, &rpc)) {
		cmn_err(CE_WARN, "DXIO Get Version RPC Failed: SMU %s, "
		    "DXIO: 0x%x", zen_smu_rpc_res_str(rpc.mdr_smu_resp),
		    rpc.mdr_dxio_resp);
		return (false);
	}

	iodie->zi_ndxio_fw = 2;
	iodie->zi_dxio_fw[0] = rpc.mdr_arg0;
	iodie->zi_dxio_fw[1] = rpc.mdr_arg1;
	iodie->zi_dxio_fw[2] = 0;
	iodie->zi_dxio_fw[3] = 0;

	return (true);
}

void
milan_report_dxio_fw_version(const zen_iodie_t *iodie)
{
	const uint8_t socno = iodie->zi_soc->zs_num;
	cmn_err(CE_CONT, "?Socket %u DXIO Version: %u.%u\n",
	    socno, iodie->zi_dxio_fw[0], iodie->zi_dxio_fw[1]);
}

bool
milan_smu_features_init(zen_iodie_t *iodie)
{
	zen_soc_t *soc = iodie->zi_soc;
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	/*
	 * Not all combinations of SMU features will result in correct system
	 * behavior, so we therefore err on the side of matching stock platform
	 * enablement -- even where that means enabling features with unknown
	 * functionality.
	 */
	const uint32_t FEATURES = MILAN_SMU_FEATURE_DATA_CALCULATION |
	    MILAN_SMU_FEATURE_THERMAL_DESIGN_CURRENT |
	    MILAN_SMU_FEATURE_THERMAL |
	    MILAN_SMU_FEATURE_PRECISION_BOOST_OVERDRIVE |
	    MILAN_SMU_FEATURE_ELECTRICAL_DESIGN_CURRENT |
	    MILAN_SMU_FEATURE_CSTATE_BOOST |
	    MILAN_SMU_FEATURE_PROCESSOR_THROTTLING_TEMPERATURE |
	    MILAN_SMU_FEATURE_CORE_CLOCK_DPM |
	    MILAN_SMU_FEATURE_FABRIC_CLOCK_DPM |
	    MILAN_SMU_FEATURE_XGMI_DYNAMIC_LINK_WIDTH_MANAGEMENT |
	    MILAN_SMU_FEATURE_DIGITAL_LDO |
	    MILAN_SMU_FEATURE_SOCCLK_DEEP_SLEEP |
	    MILAN_SMU_FEATURE_LCLK_DEEP_SLEEP |
	    MILAN_SMU_FEATURE_SYSHUBCLK_DEEP_SLEEP |
	    MILAN_SMU_FEATURE_CLOCK_GATING |
	    MILAN_SMU_FEATURE_DYNAMIC_LDO_DROPOUT_LIMITER |
	    MILAN_SMU_FEATURE_DYNAMIC_VID_OPTIMIZER |
	    MILAN_SMU_FEATURE_AGE;

	rpc.zsr_req = ZEN_SMU_OP_ENABLE_FEATURE;
	rpc.zsr_args[0] = FEATURES;

	res = zen_smu_rpc(iodie, &rpc);

	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN,
		    "Socket %u: SMU Enable Features RPC Failed: features: "
		    "0x%x, SMU %s (0x%x)", soc->zs_num, FEATURES,
		    zen_smu_rpc_res_str(res), rpc.zsr_resp);
	} else {
		cmn_err(CE_CONT, "?Socket %u SMU features 0x%08x enabled\n",
		    soc->zs_num, FEATURES);
	}

	return (res == ZEN_SMU_RPC_OK);
}

/*
 * These are called from the common code, via an entry in the Milan version of
 * Zen fabric ops vector.  The common code is responsible for the bulk of
 * initialization; we merely fill in those bits that are microarchitecture
 * specific.  Note that Milan is defined to have exactly one IO die per SoC.
 */
void
milan_fabric_topo_init(zen_fabric_t *fabric)
{
	fabric->zf_uarch_fabric = &milan_fabric;
}

void
milan_fabric_soc_init(zen_soc_t *soc)
{
	ASSERT3P(soc->zs_fabric, !=, NULL);
	milan_fabric_t *mfabric = soc->zs_fabric->zf_uarch_fabric;
	ASSERT3P(mfabric, !=, NULL);
	milan_soc_t *msoc = &mfabric->mf_socs[soc->zs_num];

	soc->zs_uarch_soc = msoc;
}

void
milan_fabric_iodie_init(zen_iodie_t *iodie)
{
	ASSERT3P(iodie->zi_soc, !=, NULL);
	milan_soc_t *msoc = iodie->zi_soc->zs_uarch_soc;
	ASSERT3P(msoc, !=, NULL);
	ASSERT3U(iodie->zi_num, ==, 0);
	milan_iodie_t *miodie = &msoc->ms_iodies[iodie->zi_num];

	iodie->zi_uarch_iodie = miodie;
}

void
milan_fabric_smu_misc_init(zen_iodie_t *iodie)
{
	milan_iodie_t *miodie = iodie->zi_uarch_iodie;

	ASSERT3P(miodie, !=, NULL);
	if (!milan_smu_rpc_read_dpm_weights(iodie,
	    miodie->mi_dpm_weights, sizeof (miodie->mi_dpm_weights))) {
		/*
		 * XXX It's unclear whether continuing is wise.
		 */
		cmn_err(CE_WARN, "SMU: failed to retrieve DPM weights");
		bzero(miodie->mi_dpm_weights, sizeof (miodie->mi_dpm_weights));
	}
}

void
milan_fabric_ioms_init(zen_ioms_t *ioms)
{
	ASSERT3P(ioms->zio_iodie, !=, NULL);
	milan_iodie_t *miodie = ioms->zio_iodie->zi_uarch_iodie;
	ASSERT3P(miodie, !=, NULL);
	const uint8_t iomsno = ioms->zio_num;
	ASSERT3U(iomsno, <, MILAN_IOMS_PER_IODIE);
	milan_ioms_t *mioms = &miodie->mi_ioms[iomsno];

	ioms->zio_uarch_ioms = mioms;

	/*
	 * IOMS 0 has a bonus two lane PCIe Gen2 core which is used for the
	 * WAFL link, or can be used as two x1 interfaces on a 1P system.
	 */
	if (iomsno == MILAN_NBIO_BONUS_IOMS) {
		ioms->zio_flags |= ZEN_IOMS_F_HAS_BONUS;
	}

	/*
	 * Milan has a 1:1 mapping between NBIOs, IOHCs and IOMSs, and all
	 * IOHCs are the same type.
	 */
	ioms->zio_nbionum = iomsno;
	ioms->zio_iohcnum = iomsno;
	ioms->zio_iohctype = ZEN_IOHCT_LARGE;

	/*
	 * nBIFs are actually associated with the NBIO instance but we have no
	 * representation in the fabric for NBIOs. In Milan there is a 1:1
	 * mapping between NBIOs and nBIFs so we flag each IOMS as also having
	 * nBIFs.
	 */
	ioms->zio_flags |= ZEN_IOMS_F_HAS_NBIF;
}

void
milan_fabric_ioms_pcie_init(zen_ioms_t *ioms)
{
	milan_ioms_t *mioms = ioms->zio_uarch_ioms;

	for (uint8_t coreno = 0; coreno < ioms->zio_npcie_cores; coreno++) {
		zen_pcie_core_t *zpc = &ioms->zio_pcie_cores[coreno];
		milan_pcie_core_t *mpc = &mioms->mio_pcie_cores[coreno];

		zpc->zpc_uarch_pcie_core = mpc;

		for (uint8_t portno = 0; portno < zpc->zpc_nports; portno++) {
			zen_pcie_port_t *port = &zpc->zpc_ports[portno];
			milan_pcie_port_t *mport = &mpc->mpc_ports[portno];

			port->zpp_uarch_pcie_port = mport;
		}
	}
}

void
milan_fabric_init_tom(zen_ioms_t *ioms, uint64_t tom, uint64_t tom2,
    uint64_t tom3)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * This register is a little funky. Bit 32 of the address has to be
	 * specified in bit 0. Otherwise, bits 31:23 are the limit.
	 */
	val = pci_getl_func(ioms->zio_pci_busno, 0, 0, IOHC_TOM);
	if (bitx64(tom, 32, 32) != 0)
		val = IOHC_TOM_SET_BIT32(val, 1);

	val = IOHC_TOM_SET_TOM(val, bitx64(tom, 31, 23));
	pci_putl_func(ioms->zio_pci_busno, 0, 0, IOHC_TOM, val);

	if (tom2 == 0)
		return;

	/*
	 * Write the upper register before the lower so we don't accidentally
	 * enable it in an incomplete fashion.
	 */
	reg = milan_ioms_reg(ioms, D_IOHC_DRAM_TOM2_HI, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_HI_SET_TOM2(val, bitx64(tom2, 40, 32));
	zen_ioms_write(ioms, reg, val);

	reg = milan_ioms_reg(ioms, D_IOHC_DRAM_TOM2_LOW, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_LOW_SET_EN(val, 1);
	val = IOHC_DRAM_TOM2_LOW_SET_TOM2(val, bitx64(tom2, 31, 23));
	zen_ioms_write(ioms, reg, val);

	if (tom3 == 0)
		return;

	reg = milan_ioms_reg(ioms, D_IOHC_DRAM_TOM3, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM3_SET_EN(val, 1);
	val = IOHC_DRAM_TOM3_SET_LIMIT(val, bitx64(tom3, 51, 22));
	zen_ioms_write(ioms, reg, val);
}

/*
 * We want to disable VGA and send all downstream accesses to its address range
 * to DRAM just as we do from the cores.  This requires clearing
 * IOHC::NB_PCI_ARB[VGA_HOLE]; for reasons unknown, the default here is
 * different from the other settings that typically default to VGA-off.  The
 * rest of this register has nothing to do with decoding and we leave its
 * contents alone.
 */
void
milan_fabric_disable_vga(zen_ioms_t *ioms)
{
	uint32_t val;

	val = pci_getl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_PCI_ARB);
	val = IOHC_NB_PCI_ARB_SET_VGA_HOLE(val, IOHC_NB_PCI_ARB_VGA_HOLE_RAM);
	pci_putl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_PCI_ARB, val);
}

/*
 * Set the IOHC PCI device's subsystem identifiers.  This could be set to the
 * baseboard's subsystem ID, but the IOHC PCI device doesn't have any
 * oxide-specific semantics so we leave it at the AMD-recommended value.  Note
 * that the POR default value is not the one AMD recommends, for whatever
 * reason.
 */
void
milan_fabric_iohc_pci_ids(zen_ioms_t *ioms)
{
	uint32_t val;

	val = pci_getl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_ADAPTER_ID_W);
	val = IOHC_NB_ADAPTER_ID_W_SET_SVID(val, VENID_AMD);
	val = IOHC_NB_ADAPTER_ID_W_SET_SDID(val,
	    IOHC_NB_ADAPTER_ID_W_AMD_MILAN_IOHC);
	pci_putl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_ADAPTER_ID_W, val);
}

void
milan_fabric_iohc_fch_link(zen_ioms_t *ioms, bool has_fch)
{
	smn_reg_t reg;

	reg = milan_ioms_reg(ioms, D_IOHC_SB_LOCATION, 0);
	if (has_fch) {
		smn_reg_t iommureg;
		uint32_t val;

		val = zen_ioms_read(ioms, reg);
		iommureg = milan_ioms_reg(ioms, D_IOMMUL1_SB_LOCATION,
		    MIL1SU_IOAGR);
		zen_ioms_write(ioms, iommureg, val);
		iommureg = milan_ioms_reg(ioms, D_IOMMUL2_SB_LOCATION, 0);
		zen_ioms_write(ioms, iommureg, val);
	} else {
		zen_ioms_write(ioms, reg, 0);
	}
}

void
milan_fabric_pcie_refclk(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_ioms_reg(ioms, D_IOHC_REFCLK_MODE, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_REFCLK_MODE_SET_27MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_25MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_100MHZ(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
milan_fabric_set_pci_to(zen_ioms_t *ioms, uint16_t limit, uint16_t delay)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_ioms_reg(ioms, D_IOHC_PCIE_CRS_COUNT, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_PCIE_CRS_COUNT_SET_LIMIT(val, limit);
	val = IOHC_PCIE_CRS_COUNT_SET_DELAY(val, delay);
	zen_ioms_write(ioms, reg, val);
}

/*
 * XXX We're using lazy defaults of what the system default has historically
 * been here for some of these. We should test and forcibly disable in
 * hardware. Probably want to manipulate IOHC::PCIE_VDM_CNTL2 at some point to
 * better figure out the VDM story. XXX
 * Also, ARI enablement is being done earlier than otherwise because we want to
 * only touch this reg in one place if we can.
 */
void
milan_fabric_iohc_features(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_ioms_reg(ioms, D_IOHC_FCTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_FCTL_SET_ARI(val, 1);
	/* XXX Wants to be IOHC_FCTL_P2P_DISABLE? */
	val = IOHC_FCTL_SET_P2P(val, IOHC_FCTL_P2P_DROP_NMATCH);
	zen_ioms_write(ioms, reg, val);
}

void
milan_fabric_iohc_arbitration(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Start with IOHC burst related entries. These are always the same
	 * across every entity. The value used for the actual time entries just
	 * varies.
	 */
	for (uint_t i = 0; i < IOHC_SION_MAX_ENTS; i++) {
		uint32_t tsval;

		reg = milan_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);

		reg = milan_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);

		switch (i) {
		case 0:
		case 1:
		case 2:
			tsval = IOHC_SION_CLIREQ_TIME_0_2_VAL;
			break;
		case 3:
		case 4:
			tsval = IOHC_SION_CLIREQ_TIME_3_4_VAL;
			break;
		case 5:
			tsval = IOHC_SION_CLIREQ_TIME_5_VAL;
			break;
		default:
			continue;
		}

		reg = milan_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_TIME_LOW, i);
		zen_ioms_write(ioms, reg, tsval);
		reg = milan_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_TIME_HI, i);
		zen_ioms_write(ioms, reg, tsval);
	}

	/*
	 * Yes, we only set [4:0] here. I know it's odd. We're actually setting
	 * S1's only instance (0) and the first 4 of the 6 instances of S0.
	 * Apparently it's not necessary to set instances 5 and 6.
	 */
	for (uint_t i = 0; i < 4; i++) {
		reg = milan_ioms_reg(ioms, D_IOHC_SION_Sn_CLI_NP_DEFICIT, i);

		val = zen_ioms_read(ioms, reg);
		val = IOHC_SION_CLI_NP_DEFICIT_SET(val,
		    IOHC_SION_CLI_NP_DEFICIT_VAL);
		zen_ioms_write(ioms, reg, val);
	}

	/*
	 * Go back and finally set the live lock watchdog to finish off the
	 * IOHC.
	 */
	reg = milan_ioms_reg(ioms, D_IOHC_SION_LLWD_THRESH, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_SION_LLWD_THRESH_SET(val, IOHC_SION_LLWD_THRESH_VAL);
	zen_ioms_write(ioms, reg, val);

	/*
	 * Next on our list is the IOAGR. While there are 5 entries, only 4 are
	 * ever set it seems.
	 */
	for (uint_t i = 0; i < 4; i++) {
		uint32_t tsval;

		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S1_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S1_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);

		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S0_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S0_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S1_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S1_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);

		switch (i) {
		case 0:
		case 1:
		case 2:
			tsval = IOAGR_SION_CLIREQ_TIME_0_2_VAL;
			break;
		case 3:
			tsval = IOAGR_SION_CLIREQ_TIME_3_VAL;
			break;
		default:
			continue;
		}

		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_TIME_LOW, i);
		zen_ioms_write(ioms, reg, tsval);
		reg = milan_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_TIME_HI, i);
		zen_ioms_write(ioms, reg, tsval);
	}

	/*
	 * The IOAGR only has the watchdog.
	 */

	reg = milan_ioms_reg(ioms, D_IOAGR_SION_LLWD_THRESH, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOAGR_SION_LLWD_THRESH_SET(val, IOAGR_SION_LLWD_THRESH_VAL);
	zen_ioms_write(ioms, reg, val);

	/*
	 * Finally, the SDPMUX variant, which is surprisingly consistent
	 * compared to everything else to date.
	 */
	for (uint_t i = 0; i < SDPMUX_SION_MAX_ENTS; i++) {
		reg = milan_ioms_reg(ioms,
		    D_SDPMUX_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms,
		    D_SDPMUX_SION_S1_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S1_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S0_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S0_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S1_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S1_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);

		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S0_CLIREQ_TIME_LOW, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_TIME_VAL);
		reg = milan_ioms_reg(ioms, D_SDPMUX_SION_S0_CLIREQ_TIME_HI, i);
		zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_TIME_VAL);
	}

	reg = milan_ioms_reg(ioms, D_SDPMUX_SION_LLWD_THRESH, 0);
	val = zen_ioms_read(ioms, reg);
	val = SDPMUX_SION_LLWD_THRESH_SET(val, SDPMUX_SION_LLWD_THRESH_VAL);
	zen_ioms_write(ioms, reg, val);

	/*
	 * XXX We probably don't need this since we don't have USB. But until we
	 * have things working and can experiment, hard to say. If someone were
	 * to use the bus, probably something we need to consider.
	 */
	reg = milan_ioms_reg(ioms, D_IOHC_USB_QOS_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_USB_QOS_CTL_SET_UNID1_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID1_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID1_ID(val, 0x30);
	val = IOHC_USB_QOS_CTL_SET_UNID0_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID0_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID0_ID(val, 0x2f);
	zen_ioms_write(ioms, reg, val);
}

void
milan_fabric_nbif_arbitration(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT2, 0);
	zen_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);
	reg = milan_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT3, 0);
	zen_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);

	reg = milan_nbif_reg(nbif, D_NBIF_BIFC_MISC_CTL0, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_BIFC_MISC_CTL0_SET_PME_TURNOFF(val,
	    NBIF_BIFC_MISC_CTL0_PME_TURNOFF_FW);
	zen_nbif_write(nbif, reg, val);
}

/*
 * Note, there is no need to change IOAGR::IOAGR_SDP_PORT_CONTROL, which is why
 * it is missing. The SDPMUX does not have an early wake up register.
 */
void
milan_fabric_sdp_control(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_ioms_reg(ioms, D_IOHC_SDP_PORT_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_SDP_PORT_CTL_SET_PORT_HYSTERESIS(val, 0xff);
	zen_ioms_write(ioms, reg, val);

	reg = milan_ioms_reg(ioms, D_IOHC_SDP_EARLY_WAKE_UP, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_SDP_EARLY_WAKE_UP_SET_HOST_ENABLE(val, 0xffff);
	val = IOHC_SDP_EARLY_WAKE_UP_SET_DMA_ENABLE(val, 0x1);
	zen_ioms_write(ioms, reg, val);

	reg = milan_ioms_reg(ioms, D_IOAGR_EARLY_WAKE_UP, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOAGR_EARLY_WAKE_UP_SET_DMA_ENABLE(val, 0x1);
	zen_ioms_write(ioms, reg, val);

	reg = milan_ioms_reg(ioms, D_SDPMUX_SDP_PORT_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = SDPMUX_SDP_PORT_CTL_SET_HOST_ENABLE(val, 0xffff);
	val = SDPMUX_SDP_PORT_CTL_SET_DMA_ENABLE(val, 0x1);
	val = SDPMUX_SDP_PORT_CTL_SET_PORT_HYSTERESIS(val, 0xff);
	zen_ioms_write(ioms, reg, val);
}

/*
 * This bit of initialization is both strange and not very well documented.
 */
void
milan_fabric_nbif_syshub_dma(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * These registers, like all SYSHUBMM registers, have no instance on
	 * nBIF2.
	 */
	if (nbif->zn_num > 1)
		return;

	/*
	 * This is only set on nBIF0.
	 */
	if (nbif->zn_num == 0) {
		reg = milan_nbif_reg(nbif, D_NBIF_ALT_BGEN_BYP_SOC, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW0(val, 1);
		zen_nbif_write(nbif, reg, val);
	}

	/*
	 * This is a bit weird whereby we only set this on nBIF1 on IOMS 0/1.
	 * Not clear why that is.
	 */
	if (nbif->zn_num == 1 && nbif->zn_ioms->zio_num <= 1) {
		reg = milan_nbif_reg(nbif, D_NBIF_ALT_BGEN_BYP_SHUB, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_BGEN_BYP_SHUB_SET_DMA_SW0(val, 1);
		zen_nbif_write(nbif, reg, val);
	}
}

/*
 * We need to initialize each IOAPIC as there is one per IOMS. First we
 * initialize the interrupt routing table. This is used to mux the various
 * legacy INTx interrupts and the bridge's interrupt to a given location. This
 * follow from the PPR.
 *
 * After that we need to go through and program the feature register for the
 * IOAPIC and its address. Because there is one IOAPIC per IOMS, one has to be
 * elected the primary and the rest, secondary. This is done based on which IOMS
 * has the FCH.
 */
void
milan_fabric_ioapic(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	for (uint_t i = 0; i < ARRAY_SIZE(milan_ioapic_routes); i++) {
		smn_reg_t reg = milan_ioms_reg(ioms, D_IOAPIC_ROUTE, i);
		uint32_t route = zen_ioms_read(ioms, reg);

		route = IOAPIC_ROUTE_SET_BRIDGE_MAP(route,
		    milan_ioapic_routes[i].zii_map);
		route = IOAPIC_ROUTE_SET_INTX_SWIZZLE(route,
		    milan_ioapic_routes[i].zii_swiz);
		route = IOAPIC_ROUTE_SET_INTX_GROUP(route,
		    milan_ioapic_routes[i].zii_group);

		zen_ioms_write(ioms, reg, route);
	}

	/*
	 * The address registers are in the IOHC while the feature registers are
	 * in the IOAPIC SMN space. To ensure that the other IOAPICs can't be
	 * enabled with reset addresses, we instead lock them. XXX Should we
	 * lock primary?
	 */
	reg = milan_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_HI, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val,
		    bitx64(ZEN_PHYSADDR_IOHC_IOAPIC, 47, 32));
	} else {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val, 0);
	}
	zen_ioms_write(ioms, reg, val);

	reg = milan_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_LO, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_LO_SET_ADDR(val,
		    bitx64(ZEN_PHYSADDR_IOHC_IOAPIC, 31, 8));
		val = IOHC_IOAPIC_ADDR_LO_SET_LOCK(val, 0);
		val = IOHC_IOAPIC_ADDR_LO_SET_EN(val, 1);
	} else {
		val = IOHC_IOAPIC_ADDR_LO_SET_ADDR(val, 0);
		val = IOHC_IOAPIC_ADDR_LO_SET_LOCK(val, 1);
		val = IOHC_IOAPIC_ADDR_LO_SET_EN(val, 0);
	}
	zen_ioms_write(ioms, reg, val);

	/*
	 * Every IOAPIC requires that we enable 8-bit addressing and that it be
	 * able to generate interrupts to the FCH. The most important bit here
	 * is the secondary bit which determines whether or not this IOAPIC is
	 * subordinate to another.
	 */
	reg = milan_ioms_reg(ioms, D_IOAPIC_FEATURES, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOAPIC_FEATURES_SET_SECONDARY(val, 0);
	} else {
		val = IOAPIC_FEATURES_SET_SECONDARY(val, 1);
	}
	val = IOAPIC_FEATURES_SET_FCH(val, 1);
	val = IOAPIC_FEATURES_SET_ID_EXT(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
milan_fabric_iohc_bus_num(zen_ioms_t *ioms, uint8_t busno)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_ioms_reg(ioms, D_IOHC_BUS_NUM_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_BUS_NUM_CTL_SET_EN(val, 1);
	val = IOHC_BUS_NUM_CTL_SET_BUS(val, busno);
	zen_ioms_write(ioms, reg, val);
}

/*
 * Go through and configure and set up devices and functions. In particular we
 * need to go through and set up the following:
 *
 *  o Strap bits that determine whether or not the function is enabled
 *  o Enabling the interrupts of corresponding functions
 *  o Setting up specific PCI device straps around multi-function, FLR, poison
 *    control, TPH settings, etc.
 *
 * XXX For getting to PCIe faster and since we're not going to use these, and
 * they're all disabled, for the moment we just ignore the straps that aren't
 * related to interrupts, enables, and cfg comps.
 */
void
milan_fabric_nbif_dev_straps(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t intr;

	reg = milan_nbif_reg(nbif, D_NBIF_INTR_LINE_EN, 0);
	intr = zen_nbif_read(nbif, reg);
	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++) {
		smn_reg_t strapreg;
		uint32_t strap;
		zen_nbif_func_t *func = &nbif->zn_funcs[funcno];

		/*
		 * This indicates that we have a dummy function or similar. In
		 * which case there's not much to do here, the system defaults
		 * are generally what we want. XXX Kind of sort of. Not true
		 * over time.
		 */
		if ((func->znf_flags & ZEN_NBIF_F_NO_CONFIG) != 0) {
			continue;
		}

		strapreg = milan_nbif_func_reg(func, D_NBIF_FUNC_STRAP0);
		strap = zen_nbif_func_read(func, strapreg);

		if ((func->znf_flags & ZEN_NBIF_F_ENABLED) != 0) {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 1);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->znf_dev, func->znf_func, 1);

			/*
			 * Strap enabled SATA devices to what AMD asks for.
			 */
			if (func->znf_type == ZEN_NBIF_T_SATA) {
				strap = NBIF_FUNC_STRAP0_SET_MAJ_REV(strap, 7);
				strap = NBIF_FUNC_STRAP0_SET_MIN_REV(strap, 1);
			}
		} else {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 0);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->znf_dev, func->znf_func, 0);
		}

		zen_nbif_func_write(func, strapreg, strap);
	}

	zen_nbif_write(nbif, reg, intr);

	/*
	 * Each nBIF has up to three devices on them, though not all of them
	 * seem to be used. However, it's suggested that we enable completion
	 * timeouts on all three device straps.
	 */
	for (uint8_t devno = 0; devno < MILAN_NBIF_MAX_DEVS; devno++) {
		smn_reg_t reg;
		uint32_t val;

		reg = milan_nbif_reg(nbif, D_NBIF_PORT_STRAP3, devno);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP3_SET_COMP_TO(val, 1);
		zen_nbif_write(nbif, reg, val);
	}
}

/*
 * There are five bridges that are associated with the NBIFs. One on NBIF0,
 * three on NBIF1, and the last on the SB. There is nothing on NBIF 2 which is
 * why we don't use the nbif iterator, though this is somewhat uglier. The
 * default expectation of the system is that the CRS bit is set. XXX these have
 * all been left enabled for now.
 */
void
milan_fabric_nbif_bridges(zen_ioms_t *ioms)
{
	uint32_t val;
	const smn_reg_t smn_regs[5] = {
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->zio_num, 0, 0),
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->zio_num, 1, 0),
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->zio_num, 1, 1),
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->zio_num, 1, 2),
		IOHCDEV_SB_BRIDGE_CTL(ioms->zio_num)
	};

	for (uint_t i = 0; i < ARRAY_SIZE(smn_regs); i++) {
		val = zen_ioms_read(ioms, smn_regs[i]);
		val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
		zen_ioms_write(ioms, smn_regs[i], val);
	}
}

static int
milan_dxio_init(zen_iodie_t *iodie, void *arg)
{
	zen_soc_t *soc = iodie->zi_soc;

	/*
	 * XXX Ethanol-X has a BMC hanging off socket 0, so on that platform we
	 * need to reload the state machine because it's already been used to do
	 * what the ABL calls early link training.  Not doing this results in
	 * this failure when we run dxio_load: DXIO Load Data Failed: Heap: 0x6,
	 * PA: 0x7ff98000, Len: 0x13e, SMU 0x1, DXIO: 0x2
	 *
	 * There's a catch: the dependency here is specifically that this is
	 * required on any socket where early link training has been done, which
	 * is controlled by an APCB token -- it's not board-dependent, although
	 * in practice the correct value for the token is permanently fixed for
	 * each board.  If the SM reload is run on a socket other than the one
	 * that has been marked for this use in the APCB, it will fail and at
	 * present that will result in not doing the rest of DXIO setup and then
	 * panicking in PCIe setup.
	 *
	 * Historically Gimlet's APCB was basically the same as Ethanol-X's,
	 * which included doing (or trying, since there's nothing connected)
	 * early link training.  That necessitated always running SM RELOAD on
	 * socket 0.  These PCIe lanes are unused and there is no BMC on
	 * Gimlet.  The current APCB does not include that option and
	 * therefore we currently only run this if the board is identified as
	 * Ethanol.
	 *
	 * We probably want to see if we can do better by figuring out whether
	 * this is needed on socket 0, 1, or neither.
	 */
	if (oxide_board_data->obd_board == OXIDE_BOARD_ETHANOLX) {
		if (soc->zs_num == 0 && !milan_dxio_rpc_sm_reload(iodie)) {
			return (1);
		}
	}


	if (!milan_dxio_rpc_init(iodie)) {
		return (1);
	}

	/*
	 * XXX These 0x4f values were kind of given to us. Do better than a
	 * magic constant, rm.
	 */
	if (!milan_dxio_rpc_clock_gating(iodie, 0x4f, 0x4f)) {
		return (1);
	}

	/*
	 * Set up a few different variables in firmware. Best guesses is that we
	 * need MILAN_DXIO_VAR_PCIE_COMPL so we can get PCIe completions to
	 * actually happen, MILAN_DXIO_VAR_SLIP_INTERVAL is disabled, but I
	 * can't say why. XXX We should probably disable NTB hotplug because we
	 * don't have them just in case something changes here.
	 */
	if (!milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_PCIE_COMPL, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_SLIP_INTERVAL, 0)) {
		return (1);
	}

	/*
	 * This seems to configure behavior when the link is going down and
	 * power off. We explicitly ask for no delay. The latter argument is
	 * about disabling another command (which we don't use), but to keep
	 * firmware in its expected path we don't set that.  Older DXIO firmware
	 * doesn't support this so we skip it there.
	 */
	if (milan_dxio_version_at_least(iodie, 45, 682) &&
	    !milan_dxio_rpc_pcie_poweroff_config(iodie, 0, false)) {
		return (1);
	}

	/*
	 * Next we set a couple of variables that are required for us to
	 * cause the state machine to pause after a couple of different stages
	 * and then also to indicate that we want to use the v1 ancillary data
	 * format.
	 */
	if (!milan_dxio_rpc_set_var(iodie, MLIAN_DXIO_VAR_RET_AFTER_MAP, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_RET_AFTER_CONF, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_ANCILLARY_V1, 1)) {
		return (1);
	}

	/*
	 * Here, it's worth calling out what we're not setting. One of which is
	 * MILAN_DXIO_VAR_MAP_EXACT_MATCH which ends up being used to cause
	 * the mapping phase to only work if there are exact matches. I believe
	 * this means that if a device has more lanes then the configured port,
	 * it wouldn't link up, which generally speaking isn't something we want
	 * to do. Similarly, since there is no S3 support here, no need to
	 * change the save and restore mode with MILAN_DXIO_VAR_S3_MODE.
	 *
	 * From here, we do want to set MILAN_DXIO_VAR_SKIP_PSP, because the PSP
	 * really doesn't need to do anything with us. We do want to enable
	 * MILAN_DXIO_VAR_PHY_PROG so the dxio engine can properly configure
	 * things.
	 *
	 * XXX Should we gamble and set things that aren't unconditionally set
	 * so we don't rely on hw defaults?
	 */
	if (!milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_PHY_PROG, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_SKIP_PSP, 1)) {
		return (0);
	}

	return (0);
}

/*
 * Here we need to assemble data for the system we're actually on. XXX Right now
 * we're just assuming we're Ethanol-X and only leveraging ancillary data from
 * the PSP.
 */
static int
milan_dxio_plat_data(zen_iodie_t *iodie, void *arg)
{
	ddi_dma_attr_t attr;
	size_t engn_size;
	pfn_t pfn;
	zen_soc_t *soc = iodie->zi_soc;
	zen_dxio_config_t *conf = &iodie->zi_dxio_conf;
	const zen_dxio_fw_platform_t *source_data;
	zen_dxio_fw_anc_data_t *anc;
	const milan_apob_phyovr_t *phy_override;
	size_t phy_len;
	int err;

	if (oxide_board_data->obd_board == OXIDE_BOARD_ETHANOLX) {
		if (soc->zs_num == 0) {
			source_data = &ethanolx_engine_s0;
		} else {
			source_data = &ethanolx_engine_s1;
		}
	} else {
		VERIFY3U(soc->zs_num, ==, 0);
		source_data = &gimlet_engine;
	}

	engn_size = sizeof (zen_dxio_fw_platform_t) +
	    source_data->zdp_nengines * sizeof (zen_dxio_fw_engine_t);
	VERIFY3U(engn_size, <=, MMU_PAGESIZE);
	conf->zdc_conf_len = engn_size;

	zen_fabric_dma_attr(&attr);
	conf->zdc_alloc_len = MMU_PAGESIZE;
	conf->zdc_conf = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->zdc_conf, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zdc_conf);
	conf->zdc_pa = mmu_ptob((uint64_t)pfn);

	bcopy(source_data, conf->zdc_conf, engn_size);

	/*
	 * We need to account for an extra 8 bytes, surprisingly. It's a good
	 * thing we have a page. Note, dxio wants this in uint32_t units. We do
	 * that when we make the RPC call. Finally, we want to make sure that if
	 * we're in an incomplete word, that we account for that in the length.
	 */
	conf->zdc_conf_len += 8;
	conf->zdc_conf_len = P2ROUNDUP(conf->zdc_conf_len, 4);

	phy_override = kapob_find(APOB_GROUP_FABRIC,
	    MILAN_APOB_FABRIC_PHY_OVERRIDE, 0, &phy_len, &err);
	if (phy_override == NULL) {
		if (err == ENOENT) {
			return (0);
		}

		cmn_err(CE_WARN, "failed to find phy override table in APOB: "
		    "0x%x", err);
		return (1);
	}
	if (phy_len < offsetof(milan_apob_phyovr_t, map_data[0])) {
		cmn_err(CE_WARN, "APOB phy override table is too short "
		    "(actual size 0x%lx)", phy_len);
		return (1);
	}

	/*
	 * The actual length of phy data is in map_datalen; it must be no larger
	 * than the maximum and must fit in the APOB entry.
	 */
	if (phy_override->map_datalen > MILAN_APOB_PHY_OVERRIDE_MAX_LEN ||
	    phy_override->map_datalen >
	    phy_len - offsetof(milan_apob_phyovr_t, map_data[0])) {
		cmn_err(CE_WARN, "APOB phy override table data doesn't fit "
		    "(datalen = 0x%x, entry len = 0x%lx)",
		    phy_override->map_datalen, phy_len);
		return (1);
	}

	/*
	 * The headers for the ancillary heap and payload must be 4 bytes in
	 * size.
	 */
	CTASSERT(sizeof (zen_dxio_fw_anc_data_t) == 4);

	conf->zdc_anc = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->zdc_anc, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zdc_anc);
	conf->zdc_anc_pa = mmu_ptob((uint64_t)pfn);

	/*
	 * First we need to program the initial descriptor. Its type is one of
	 * the Heap types. Yes, this is different from the sub data payloads
	 * that we use. Yes, this is different from the way that the engine
	 * config data is laid out. Each entry has the amount of space they take
	 * up. Confusingly, it seems that the top entry does not include the
	 * space its header takes up. However, the subsequent payloads do.
	 */
	anc = conf->zdc_anc;
	anc->zdad_type = MILAN_DXIO_HEAP_ANCILLARY;
	anc->zdad_vers = ZEN_DXIO_FW_ANCILLARY_VERSION;
	anc->zdad_nu32s = (sizeof (zen_dxio_fw_anc_data_t) +
	    phy_override->map_datalen) >> 2;
	anc++;
	anc->zdad_type = ZEN_DXIO_FW_ANCILLARY_T_PHY;
	anc->zdad_vers = ZEN_DXIO_FW_ANCILLARY_PAYLOAD_VERSION;
	anc->zdad_nu32s = (sizeof (zen_dxio_fw_anc_data_t) +
	    phy_override->map_datalen) >> 2;
	anc++;
	bcopy(phy_override->map_data, anc, phy_override->map_datalen);
	conf->zdc_anc_len = phy_override->map_datalen +
	    2 * sizeof (zen_dxio_fw_anc_data_t);

	return (0);
}

static int
milan_dxio_load_data(zen_iodie_t *iodie, void *arg)
{
	zen_dxio_config_t *conf = &iodie->zi_dxio_conf;

	/*
	 * Begin by loading the NULL capabilities before we load any data heaps.
	 */
	if (!milan_dxio_rpc_load_caps(iodie)) {
		return (1);
	}

	if (conf->zdc_anc != NULL && !milan_dxio_rpc_load_data(iodie,
	    MILAN_DXIO_HEAP_ANCILLARY, conf->zdc_anc_pa, conf->zdc_anc_len,
	    0)) {
		return (1);
	}

	/*
	 * It seems that we're required to load both of these heaps with the
	 * mystery bit set to one. It's called that because we don't know what
	 * it does; however, these heaps are always loaded with no data, even
	 * though ancillary is skipped if there is none.
	 */
	if (!milan_dxio_rpc_load_data(iodie, MILAN_DXIO_HEAP_MACPCS, 0, 0, 1) ||
	    !milan_dxio_rpc_load_data(iodie, MILAN_DXIO_HEAP_GPIO, 0, 0, 1)) {
		return (1);
	}

	/*
	 * Load our real data!
	 */
	if (!milan_dxio_rpc_load_data(iodie, MILAN_DXIO_HEAP_ENGINE_CONFIG,
	    conf->zdc_pa, conf->zdc_conf_len, 0)) {
		return (1);
	}

	return (0);
}

static int
milan_dxio_more_conf(zen_iodie_t *iodie, void *arg)
{
	/*
	 * Note, here we might use milan_dxio_rpc_conf_training() if we want to
	 * override any of the properties there. But the defaults in DXIO
	 * firmware seem to be used by default. We also might apply various
	 * workarounds that we don't seem to need to
	 * (MILAN_DXIO_RT_SET_CONF_DXIO_WA, MILAN_DXIO_RT_SET_CONF_SPC_WA,
	 * MILAN_DXIO_RT_SET_CONF_FC_CRED_WA_DIS).
	 */

	/*
	 * XXX Do we care about any of the following:
	 *    o MILAN_DXIO_RT_SET_CONF_TX_CLOCK
	 *    o MILAN_DXIO_RT_SET_CONF_SRNS
	 *    o MILAN_DXIO_RT_SET_CONF_DLF_WA_DIS
	 *
	 * I wonder why we don't enable MILAN_DXIO_RT_SET_CONF_CE_SRAM_ECC in
	 * the old world.
	 */

	/*
	 * This is set to 1 by default because we want 'latency behaviour' not
	 * 'improved latency'.
	 */
	if (!milan_dxio_rpc_misc_rt_conf(iodie,
	    MILAN_DXIO_RT_SET_CONF_TX_FIFO_MODE, 1)) {
		return (1);
	}

	return (0);
}


/*
 * Given all of the engines on an I/O die, try and map each one to a
 * corresponding IOMS and bridge. We only care about an engine if it is a PCIe
 * engine. Note, because each I/O die is processed independently, this only
 * operates on a single I/O die.
 */
static bool
milan_dxio_map_engines(zen_fabric_t *fabric, zen_iodie_t *iodie)
{
	bool ret = true;
	zen_dxio_fw_platform_t *plat = iodie->zi_dxio_conf.zdc_conf;

	for (uint_t i = 0; i < plat->zdp_nengines; i++) {
		zen_dxio_fw_engine_t *en = &plat->zdp_engines[i];
		zen_pcie_core_t *pc;
		zen_pcie_port_t *port;
		uint8_t portno;

		if (en->zde_type != ZEN_DXIO_FW_ENGINE_PCIE)
			continue;

		pc = zen_fabric_find_pcie_core_by_lanes(iodie,
		    en->zde_start_lane, en->zde_end_lane);
		if (pc == NULL) {
			cmn_err(CE_WARN, "failed to map engine %u [%u, %u] to "
			    "a PCIe core", i, en->zde_start_lane,
			    en->zde_end_lane);
			ret = false;
			continue;
		}

		portno = en->zde_config.zdc_pcie.zdcp_mac_port_id;
		if (portno >= pc->zpc_nports) {
			cmn_err(CE_WARN, "failed to map engine %u [%u, %u] to "
			    "a PCIe port: found nports %u, but mapped to "
			    "port %u",  i, en->zde_start_lane,
			    en->zde_end_lane, pc->zpc_nports, portno);
			ret = false;
			continue;
		}

		port = &pc->zpc_ports[portno];
		if (port->zpp_dxio_engine != NULL) {
			cmn_err(CE_WARN, "engine %u [%u, %u] mapped to "
			    "port %u, which already has an engine [%u, %u]",
			    i, en->zde_start_lane, en->zde_end_lane,
			    pc->zpc_nports,
			    port->zpp_dxio_engine->zde_start_lane,
			    port->zpp_dxio_engine->zde_end_lane);
			ret = false;
			continue;
		}

		port->zpp_flags |= ZEN_PCIE_PORT_F_MAPPED;
		port->zpp_dxio_engine = en;
		pc->zpc_flags |= ZEN_PCIE_CORE_F_USED;
		if (en->zde_config.zdc_pcie.zdcp_caps.zdlc_hp !=
		    ZEN_DXIO_FW_HOTPLUG_T_DISABLED) {
			pc->zpc_flags |= ZEN_PCIE_CORE_F_HAS_HOTPLUG;
		}
	}

	return (ret);
}

/*
 * These PCIe straps need to be set after mapping is done, but before link
 * training has started. While we do not understand in detail what all of these
 * registers do, we've split this broadly into 2 categories:
 * 1) Straps where:
 *     a) the defaults in hardware seem to be reasonable given our (sometimes
 *     limited) understanding of their function
 *     b) are not features/parameters that we currently care specifically about
 *     one way or the other
 *     c) and we are currently ok with the defaults changing out from underneath
 *     us on different hardware revisions unless proven otherwise.
 * or 2) where:
 *     a) We care specifically about a feature enough to ensure that it is set
 *     (e.g. AERs) or purposefully disabled (e.g. I2C_DBG_EN)
 *     b) We are not ok with these changing based on potentially different
 *     defaults set in different hardware revisions
 * For 1), we've chosen to leave them based on whatever the hardware has chosen
 * as the default, while all the straps detailed underneath fall into category
 * 2. Note that this list is by no means definitive, and will almost certainly
 * change as our understanding of what we require from the hardware evolves.
 *
 * These can be matched to a board identifier, I/O die DF node ID, NBIO/IOMS
 * number, PCIe core number (pcie_core_t.zpc_coreno), and PCIe port number
 * (pcie_port_t.zpp_portno).  The board sentinel value is 0 and may be
 * omitted, but the others require nonzero sentinels as 0 is a valid index.  The
 * sentinel values of 0xFF here cannot match any real NBIO, RC, or port: there
 * are at most 4 NBIOs per die, 3 RC per NBIO, and 8 ports (bridges) per RC.
 * The RC and port filters are meaningful only if the corresponding strap exists
 * at the corresponding level.  The node ID, which incorporates both socket and
 * die number (die number is always 0 for Milan), is 8 bits so in principle it
 * could be 0xFF and we use 32 bits there instead.  While it's still 8 bits in
 * Genoa, AMD have reserved another 8 bits that are likely to be used in future
 * families so we opt to go all the way to 32 here.  This can be reevaluated
 * when this is refactored to support multiple families.
 */

/*
 * PCIe Straps that we unconditionally set to 1
 */
static const uint32_t milan_pcie_strap_enable[] = {
	MILAN_STRAP_PCIE_MSI_EN,
	MILAN_STRAP_PCIE_AER_EN,
	MILAN_STRAP_PCIE_GEN2_FEAT_EN,
	/* We want completion timeouts */
	MILAN_STRAP_PCIE_CPL_TO_EN,
	MILAN_STRAP_PCIE_TPH_EN,
	MILAN_STRAP_PCIE_MULTI_FUNC_EN,
	MILAN_STRAP_PCIE_DPC_EN,
	MILAN_STRAP_PCIE_ARI_EN,
	MILAN_STRAP_PCIE_PL_16G_EN,
	MILAN_STRAP_PCIE_LANE_MARGIN_EN,
	MILAN_STRAP_PCIE_LTR_SUP,
	MILAN_STRAP_PCIE_LINK_BW_NOTIF_SUP,
	MILAN_STRAP_PCIE_GEN3_1_FEAT_EN,
	MILAN_STRAP_PCIE_GEN4_FEAT_EN,
	MILAN_STRAP_PCIE_ECRC_GEN_EN,
	MILAN_STRAP_PCIE_ECRC_CHECK_EN,
	MILAN_STRAP_PCIE_CPL_ABORT_ERR_EN,
	MILAN_STRAP_PCIE_INT_ERR_EN,
	MILAN_STRAP_PCIE_RXP_ACC_FULL_DIS,

	/* ACS straps */
	MILAN_STRAP_PCIE_ACS_EN,
	MILAN_STRAP_PCIE_ACS_SRC_VALID,
	MILAN_STRAP_PCIE_ACS_TRANS_BLOCK,
	MILAN_STRAP_PCIE_ACS_DIRECT_TRANS_P2P,
	MILAN_STRAP_PCIE_ACS_P2P_CPL_REDIR,
	MILAN_STRAP_PCIE_ACS_P2P_REQ_RDIR,
	MILAN_STRAP_PCIE_ACS_UPSTREAM_FWD,
};

/*
 * PCIe Straps that we unconditionally set to 0
 * These are generally debug and test settings that are usually not a good idea
 * in my experience to allow accidental enablement.
 */
static const uint32_t milan_pcie_strap_disable[] = {
	MILAN_STRAP_PCIE_I2C_DBG_EN,
	MILAN_STRAP_PCIE_DEBUG_RXP,
	MILAN_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST,
	MILAN_STRAP_PCIE_ERR_REPORT_DIS,
	MILAN_STRAP_PCIE_TX_TEST_ALL,
	MILAN_STRAP_PCIE_MCAST_EN,
};

/*
 * PCIe Straps that have other values.
 */
static const zen_pcie_strap_setting_t milan_pcie_strap_settings[] = {
	{
		.strap_reg = MILAN_STRAP_PCIE_EQ_DS_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_EQ_US_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_16GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_16GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_5,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_SUBVID,
		.strap_data = PCI_VENDOR_ID_OXIDE,
		.strap_boardmatch = OXIDE_BOARD_GIMLET,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_SUBDID,
		.strap_data = PCI_SDID_OXIDE_GIMLET_BASE,
		.strap_boardmatch = OXIDE_BOARD_GIMLET,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
};

/*
 * PCIe Straps that exist on a per-port level.  Most pertain to the port itself;
 * others pertain to features exposed via the associated bridge.
 */
static const zen_pcie_strap_setting_t milan_pcie_port_settings[] = {
	{
		.strap_reg = MILAN_STRAP_PCIE_P_EXT_FMT_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_E2E_TLP_PREFIX_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_10B_TAG_CMPL_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_10B_TAG_REQ_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_TCOMMONMODE_TIME,
		.strap_data = 0xa,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_TPON_SCALE,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_TPON_VALUE,
		.strap_data = 0xf,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_DLF_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_DLF_EXCHANGE_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_FOM_TIME,
		.strap_data = MILAN_STRAP_PCIE_P_FOM_300US,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_SPC_MODE_8GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_SRIS_EN,
		.strap_data = 1,
		.strap_boardmatch = OXIDE_BOARD_GIMLET,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP,
		.strap_data = 0,
		.strap_boardmatch = OXIDE_BOARD_GIMLET,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP,
		.strap_data = 0,
		.strap_boardmatch = OXIDE_BOARD_GIMLET,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = MILAN_STRAP_PCIE_P_L0s_EXIT_LAT,
		.strap_data = PCIE_LINKCAP_L0S_EXIT_LAT_MAX >> 12,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	}
};

static void
milan_fabric_write_pcie_strap(zen_pcie_core_t *pc,
    const uint32_t reg, const uint32_t data)
{
	smn_reg_t a_reg, d_reg;

	a_reg = milan_pcie_core_reg(pc, D_PCIE_RSMU_STRAP_ADDR);
	d_reg = milan_pcie_core_reg(pc, D_PCIE_RSMU_STRAP_DATA);

	mutex_enter(&pc->zpc_strap_lock);
	zen_pcie_core_write(pc, a_reg, MILAN_STRAP_PCIE_ADDR_UPPER + reg);
	zen_pcie_core_write(pc, d_reg, data);
	mutex_exit(&pc->zpc_strap_lock);
}

/*
 * Here we set up all the straps for PCIe features that we care about and want
 * advertised as capabilities. Note that we do not enforce any order between the
 * straps. It is our understanding that the straps themselves do not kick off
 * any change, but instead another stage (presumably before link training)
 * initializes the read of all these straps in one go.
 * Currently, we set these straps on all cores and all ports regardless of
 * whether they are used, though this may be changed if it proves problematic.
 * We do however operate on a single I/O die at a time, because we are called
 * out of the DXIO state machine which also operates on a single I/O die at a
 * time, unless our argument is NULL.  This allows us to avoid changing strap
 * values on 2S machines for entities that were already configured completely
 * during socket 0's DXIO SM.
 */
static int
milan_fabric_init_pcie_straps(zen_pcie_core_t *pc, void *arg)
{
	const zen_iodie_t *iodie = (const zen_iodie_t *)arg;

	if (iodie != NULL && pc->zpc_ioms->zio_iodie != iodie)
		return (0);

	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_strap_enable); i++) {
		milan_fabric_write_pcie_strap(pc,
		    milan_pcie_strap_enable[i], 0x1);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_strap_disable); i++) {
		milan_fabric_write_pcie_strap(pc,
		    milan_pcie_strap_disable[i], 0x0);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_strap_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &milan_pcie_strap_settings[i];

		if (zen_fabric_pcie_strap_matches(pc, PCIE_PORTMATCH_ANY,
		    strap)) {
			milan_fabric_write_pcie_strap(pc,
			    strap->strap_reg, strap->strap_data);
		}
	}

	/* Handle Special case for DLF which needs to be set on non WAFL */
	if (pc->zpc_coreno != MILAN_IOMS_BONUS_PCIE_CORENO) {
		milan_fabric_write_pcie_strap(pc, MILAN_STRAP_PCIE_DLF_EN, 1);
	}

	/* Handle per bridge initialization */
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_port_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &milan_pcie_port_settings[i];
		for (uint_t j = 0; j < pc->zpc_nports; j++) {
			if (zen_fabric_pcie_strap_matches(pc, j, strap)) {
				milan_fabric_write_pcie_strap(pc,
				    strap->strap_reg +
				    (j * MILAN_STRAP_PCIE_NUM_PER_PORT),
				    strap->strap_data);
			}
		}
	}

	return (0);
}

static int
milan_fabric_setup_pcie_core_dbg(zen_pcie_core_t *pc, void *arg)
{
	for (uint16_t portno = 0; portno < pc->zpc_nports; portno++) {
		zen_pcie_port_t *port = &pc->zpc_ports[portno];

		if (port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) {
			smn_reg_t reg;
			uint32_t val;
			uint8_t laneno;

			/*
			 * This is the first mapped port in this core.  Enable
			 * core-level debugging capture for this port, and only
			 * this port.
			 */
			reg = milan_pcie_core_reg(pc, D_PCIE_CORE_DBG_CTL);
			val = zen_pcie_core_read(pc, reg);
			val = PCIE_CORE_DBG_CTL_SET_PORT_EN(val, 1U << portno);
			zen_pcie_core_write(pc, reg, val);

			/*
			 * Find the lowest-numbered core lane index in this port
			 * and set up lane-level debugging capture for that
			 * lane.  We could instead set this to the bitmask of
			 * all the lanes in this port, but many of the values
			 * captured are not counting statistics and it's unclear
			 * what this would do -- it's quite likely that we would
			 * end up with the bitwise OR of the values we'd get for
			 * each lane, which isn't useful.
			 *
			 * We ignore reversal here, because our only real goal
			 * is to make sure the lane we select is part of the
			 * port we selected above.  Whether it's the "first" or
			 * "last", assuming that the "first" might provide us
			 * with additional useful data about the training and
			 * width negotiation process, is difficult to know
			 * without some additional experimentation.  We may also
			 * want to consider whether in-package lane reversal
			 * should be treated differently from on-board reversal.
			 * For now we just select the lane with the lowest index
			 * at the core.  If this ends up being needed for e.g.
			 * an SI investigation, it will likely require some
			 * additional knob to select a specific lane of
			 * interest.
			 */
			laneno = port->zpp_dxio_engine->zde_start_lane -
			    pc->zpc_dxio_lane_start;
			reg = milan_pcie_core_reg(pc, D_PCIE_CORE_LC_DBG_CTL);
			val = zen_pcie_core_read(pc, reg);
			val = PCIE_CORE_LC_DBG_CTL_SET_LANE_MASK(val,
			    1U << laneno);
			zen_pcie_core_write(pc, reg, val);

			break;
		}
	}

	return (0);
}

/*
 * Here we are, it's time to actually kick off the state machine that we've
 * wanted to do.
 */
static int
milan_dxio_state_machine(zen_iodie_t *iodie, void *arg)
{
	milan_iodie_t *miodie = iodie->zi_uarch_iodie;
	zen_soc_t *soc = iodie->zi_soc;
	zen_fabric_t *fabric = iodie->zi_soc->zs_fabric;

	if (!milan_dxio_rpc_sm_start(iodie)) {
		return (1);
	}

	for (;;) {
		milan_dxio_reply_t reply = { 0 };

		if (!milan_dxio_rpc_sm_getstate(iodie, &reply)) {
			return (1);
		}

		switch (reply.mds_type) {
		case MILAN_DXIO_DATA_TYPE_SM:
			cmn_err(CE_CONT, "?Socket %u LISM 0x%x->0x%x\n",
			    soc->zs_num, miodie->mi_state, reply.mds_arg0);
			miodie->mi_state = reply.mds_arg0;
			switch (miodie->mi_state) {
			/*
			 * The mapped state indicates that the engines and lanes
			 * that we have provided in our DXIO configuration have
			 * been mapped back to the actual set of PCIe ports on
			 * the IOMS (e.g. G0, P0) and specific bridge indexes
			 * within that port group. The very first thing we need
			 * to do here is to figure out what actually has been
			 * mapped to what and update what ports are actually
			 * being used by devices or not.
			 */
			case MILAN_DXIO_SM_MAPPED:
				zen_pcie_populate_dbg(fabric,
				    MPCS_DXIO_SM_MAPPED, iodie->zi_node_id);

				if (!milan_dxio_rpc_retrieve_engine(iodie)) {
					return (1);
				}

				if (!milan_dxio_map_engines(fabric, iodie)) {
					cmn_err(CE_WARN, "Socket %u LISM: "
					    "failed to map all DXIO engines to "
					    "devices.  PCIe will not function",
					    soc->zs_num);
					return (1);
				}

				/*
				 * XXX There is a substantial body of additional
				 * things that can be done here; investigation
				 * is needed.
				 */

				/*
				 * Now that we have the mapping done, we set up
				 * the straps for PCIe.
				 */
				(void) zen_fabric_walk_pcie_core(fabric,
				    milan_fabric_init_pcie_straps, iodie);
				cmn_err(CE_CONT, "?Socket %u LISM: Finished "
				    "writing PCIe straps\n", soc->zs_num);

				/*
				 * Set up the core-level debugging controls so
				 * that we get extended data for the first port
				 * in the core that's been mapped.
				 */
				(void) zen_fabric_walk_pcie_core(fabric,
				    milan_fabric_setup_pcie_core_dbg, NULL);

				zen_pcie_populate_dbg(fabric,
				    MPCS_DXIO_SM_MAPPED_RESUME,
				    iodie->zi_node_id);
				break;
			case MILAN_DXIO_SM_CONFIGURED:
				zen_pcie_populate_dbg(fabric,
				    MPCS_DXIO_SM_CONFIGURED,
				    iodie->zi_node_id);

				/*
				 * XXX There is a substantial body of additional
				 * things that can be done here; investigation
				 * is needed.
				 */

				zen_pcie_populate_dbg(fabric,
				    MPCS_DXIO_SM_CONFIGURED_RESUME,
				    iodie->zi_node_id);
				break;
			case MILAN_DXIO_SM_DONE:
				/*
				 * We made it. Somehow we're done!
				 */
				cmn_err(CE_CONT, "?Socket %u LISM: done\n",
				    soc->zs_num);
				goto done;
			default:
				/*
				 * For most states there doesn't seem to be much
				 * to do. So for now we just leave the default
				 * case to continue and proceed to the next
				 * state machine state.
				 */
				break;
			}
			break;
		case MILAN_DXIO_DATA_TYPE_RESET:
			zen_pcie_populate_dbg(fabric,
			    MPCS_DXIO_SM_PERST, iodie->zi_node_id);
			cmn_err(CE_CONT, "?Socket %u LISM: PERST %x, %x\n",
			    soc->zs_num, reply.mds_arg0, reply.mds_arg1);
			if (reply.mds_arg0 == 0) {
				cmn_err(CE_NOTE, "Socket %u LISM: disregarding "
				    "request to assert PERST at index 0x%x",
				    soc->zs_num, reply.mds_arg1);
				break;
			}

			if (oxide_board_data->obd_board ==
			    OXIDE_BOARD_ETHANOLX) {

				/*
				 * Release PERST manually on Ethanol-X which
				 * requires it.  PCIE_RSTn_L shares pins with
				 * the following GPIOs:
				 *
				 * FCH::GPIO::GPIO_26 FCH::GPIO::GPIO_27
				 * FCH::RMTGPIO::GPIO_266 FCH::RMTGPIO::GPIO_267
				 *
				 * If we were going to support this generically,
				 * these should probably be part of the board
				 * definition.  They should also be DPIOs, but
				 * we probably can't use the DPIO subsystem
				 * itself yet.
				 *
				 * XXX The only other function on these pins is
				 * the PCIe reset itself.  We assume the mux is
				 * passing the GPIO function at this point: if
				 * it's not, this will do nothing unless we
				 * invoke ZHGOP_CONFIGURE first.  This also
				 * works only for socket 0; we can't access the
				 * FCH on socket 1 because won't let us use SMN
				 * and we haven't set up the secondary FCH
				 * aperture here.  This most likely means the
				 * NVMe sockets won't work.
				 */
				if (iodie->zi_node_id == 0) {
					milan_hack_gpio(ZHGOP_SET, 26);
					milan_hack_gpio(ZHGOP_SET, 27);
					milan_hack_gpio(ZHGOP_SET, 266);
					milan_hack_gpio(ZHGOP_SET, 267);
				}
			}

			zen_pcie_populate_dbg(fabric,
			    MPCS_DXIO_SM_PERST_RESUME,
			    iodie->zi_node_id);

			break;
		case MILAN_DXIO_DATA_TYPE_NONE:
			cmn_err(CE_WARN, "Socket %u LISM: Got the none data "
			    "type... are we actually done?", soc->zs_num);
			goto done;
		default:
			cmn_err(CE_WARN, "Socket %u LISM: Got unexpected DXIO "
			    "return type 0x%x. PCIe will not function.",
			    soc->zs_num, reply.mds_type);
			return (1);
		}

		if (!milan_dxio_rpc_sm_resume(iodie)) {
			return (1);
		}
	}

done:
	zen_pcie_populate_dbg(fabric, MPCS_DXIO_SM_DONE,
	    iodie->zi_node_id);

	if (!milan_dxio_rpc_retrieve_engine(iodie)) {
		return (1);
	}

	return (0);
}

/*
 * Here we are going through bridges and need to start setting them up with the
 * various features that we care about. Most of these are an attempt to have
 * things set up so PCIe enumeration can meaningfully actually use these. The
 * exact set of things required is ill-defined. Right now this includes:
 *
 *   o Enabling the bridges such that they can actually allow software to use
 *     them. XXX Though really we should disable DMA until such a time as we're
 *     OK with that.
 *
 *   o Changing settings that will allow the links to actually flush TLPs when
 *     the link goes down.
 */
static int
milan_fabric_init_bridges(zen_pcie_port_t *port, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	bool hide;
	const zen_pcie_core_t *pc = port->zpp_core;
	const zen_ioms_t *ioms = pc->zpc_ioms;

	/*
	 * We need to determine whether or not this bridge should be considered
	 * visible. This is messy. Ideally, we'd just have every bridge be
	 * visible; however, life isn't that simple because convincing the PCIe
	 * engine that it should actually allow for completion timeouts to
	 * function as expected. In addition, having bridges that have no
	 * devices present and never can due to the platform definition can end
	 * up being rather wasteful of precious 32-bit non-prefetchable memory.
	 * The current masking rules are based on what we have learned from
	 * trial and error works.
	 *
	 * Strictly speaking, a bridge will work from a completion timeout
	 * perspective if the SMU thinks it belongs to a PCIe port that has any
	 * hotpluggable elements or otherwise has a device present.
	 * Unfortunately the case you really want to work, a non-hotpluggable,
	 * but defined device that does not have a device present should be
	 * visible does not work.
	 *
	 * Ultimately, what we have implemented here is to basically say if a
	 * bridge is not mapped to an endpoint, then it is not shown. If it is,
	 * and it belongs to a hot-pluggable port then we always show it.
	 * Otherwise we only show it if there's a device present.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0) {
		bool hotplug, trained;
		uint8_t lt;

		hotplug = (pc->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG) != 0;
		lt = port->zpp_dxio_engine->zde_config.zdc_pcie.zdcp_link_train;
		trained = lt == MILAN_DXIO_PCIE_SUCCESS;
		hide = !hotplug && !trained;
	} else {
		hide = true;
	}

	if (hide) {
		port->zpp_flags |= ZEN_PCIE_PORT_F_BRIDGE_HIDDEN;
	}

	reg = milan_pcie_port_reg(port, D_IOHCDEV_PCIE_BRIDGE_CTL);
	val = zen_pcie_port_read(port, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	if (hide) {
		val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 1);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 1);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 1);
	} else {
		val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 0);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 0);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 0);
	}
	zen_pcie_port_write(port, reg, val);

	reg = milan_pcie_port_reg(port, D_PCIE_PORT_TX_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_CTL_SET_TLP_FLUSH_DOWN_DIS(val, 0);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure the hardware knows the corresponding b/d/f for this bridge.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_TX_ID);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_ID_SET_BUS(val, ioms->zio_pci_busno);
	val = PCIE_PORT_TX_ID_SET_DEV(val, port->zpp_device);
	val = PCIE_PORT_TX_ID_SET_FUNC(val, port->zpp_func);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Next, we have to go through and set up a bunch of the lane controller
	 * configuration controls for the individual port. These include
	 * various settings around how idle transitions occur, how it replies to
	 * certain messages, and related.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL_SET_L1_IMM_ACK(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_L0S_L1_TRAIN(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_WIDTH_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_DUAL_RECONFIG(val, 1);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_RENEG_EN(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_CTL2);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL2_SET_ELEC_IDLE(val,
	    PCIE_PORT_LC_CTL2_ELEC_IDLE_M1);
	/*
	 * This is supposed to be set as part of some workaround for ports that
	 * support at least PCIe Gen 3.0 speeds. As all supported platforms
	 * (gimlet, Ethanol-X, etc.) always support that on the port unless this
	 * is one of the WAFL related lanes, we always set this.
	 */
	if (pc->zpc_coreno != MILAN_IOMS_BONUS_PCIE_CORENO) {
		val = PCIE_PORT_LC_CTL2_SET_TS2_CHANGE_REQ(val,
		    PCIE_PORT_LC_CTL2_TS2_CHANGE_128);
	}
	zen_pcie_port_write(port, reg, val);

	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_CTL3);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Lucky Hardware Debug 15. Why is it lucky? Because all we know is
	 * we've been told to set it.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_HW_DBG);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_HW_DBG_SET_DBG15(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure the 8 GT/s symbols per clock is set to 2.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_CTL6);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL6_SET_SPC_MODE_8GT(val,
	    PCIE_PORT_LC_CTL6_SPC_MODE_8GT_2);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Software expects to see the PCIe slot implemented bit when a slot
	 * actually exists. For us, this is basically anything that actually is
	 * considered MAPPED. Set that now on the port.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0) {
		uint16_t reg;

		reg = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, MILAN_BRIDGE_R_PCI_PCIE_CAP);
		reg |= PCIE_PCIECAP_SLOT_IMPL;
		pci_putw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, MILAN_BRIDGE_R_PCI_PCIE_CAP, reg);
	}

	return (0);
}

/*
 * This is a companion to milan_fabric_init_bridges, that operates on the PCIe
 * core level before we get to the individual bridge. This initialization
 * generally is required to ensure that each port (regardless of whether it's
 * hidden or not) is able to properly generate an all 1s response. In addition
 * we have to take care of things like atomics, idling defaults, certain
 * receiver completion buffer checks, etc.
 */
static int
milan_fabric_init_pcie_core(zen_pcie_core_t *pc, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_CI_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_CI_CTL_SET_LINK_DOWN_CTO_EN(val, 1);
	val = PCIE_CORE_CI_CTL_SET_IGN_LINK_DOWN_CTO_ERR(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Program the base SDP unit ID for this core. The unit ID for each
	 * port within the core is the base ID plus the port number.
	 */
	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_SDP_CTL);
	val = zen_pcie_core_read(pc, reg);
	/*
	 * The unit ID is split into two parts, and written to different
	 * fields in this register.
	 */
	ASSERT0(pc->zpc_sdp_unit & 0x8000000);
	val = PCIE_CORE_SDP_CTL_SET_UNIT_ID_HI(val,
	    bitx8(pc->zpc_sdp_unit, 6, 3));
	val = PCIE_CORE_SDP_CTL_SET_UNIT_ID_LO(val,
	    bitx8(pc->zpc_sdp_unit, 2, 0));
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Program values required for receiver margining to work. These are
	 * hidden in the core. Milan processors generally only support timing
	 * margining as that's what's required by PCIe Gen 4. Voltage margining
	 * was made mandatory in Gen 5.
	 *
	 * The first register (D_PCIE_CORE_RX_MARGIN_CTL_CAP) sets up the
	 * supported margining. The second register (D_PCIE_CORE_RX_MARGIN1)
	 * sets the supported offsets and steps. These values are given us by
	 * AMD in a roundabout fashion. These values translate into allowing the
	 * maximum timing offset to be 50% of a UI (unit interval) and taking up
	 * to 23 steps in either direction. Because we've set the maximum offset
	 * to be 50%, each step takes 50%/23 or ~2.17%. The third register
	 * (D_PCIE_CORE_RX_MARGIN2) is used to set how many lanes can be
	 * margined at the same time. Similarly we've been led to believe the
	 * entire core supports margining at once, so that's 16 lanes and the
	 * register is encoded as a zeros based value (so that's why we write
	 * 0xf).
	 */
	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN_CTL_CAP);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_TIME(val, 1);
	zen_pcie_core_write(pc, reg, val);

	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN1);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN1_SET_MAX_TIME_OFF(val, 0x32);
	val = PCIE_CORE_RX_MARGIN1_SET_NUM_TIME_STEPS(val, 0x17);
	zen_pcie_core_write(pc, reg, val);

	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN2);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN2_SET_NLANES(val, 0xf);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Ensure that RCB checking is what's seemingly expected.
	 */
	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_PCIE_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_CTL_SET_RCB_BAD_ATTR_DIS(val, 1);
	val = PCIE_CORE_PCIE_CTL_SET_RCB_BAD_SIZE_DIS(val, 0);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Enabling atomics in the RC requires a few different registers. Both
	 * a strap has to be overridden and then corresponding control bits.
	 */
	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_STRAP_F0);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_ROUTE(val, 1);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_EN(val, 1);
	zen_pcie_core_write(pc, reg, val);

	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_PCIE_CTL2);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_CTL2_TX_ATOMIC_ORD_DIS(val, 1);
	val = PCIE_CORE_PCIE_CTL2_TX_ATOMIC_OPS_DIS(val, 0);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Ensure the correct electrical idle mode detection is set. In
	 * addition, it's been recommended we ignore the K30.7 EDB (EnD Bad)
	 * special symbol errors.
	 */
	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_PCIE_P_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_P_CTL_SET_ELEC_IDLE(val,
	    PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M1);
	val = PCIE_CORE_PCIE_P_CTL_SET_IGN_EDB_ERR(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * The IOMMUL1 does not have an instance for the on-the side WAFL lanes.
	 * Skip the WAFL port if we're that.
	 */
	if (pc->zpc_coreno >= IOMMUL1_N_PCIE_CORES)
		return (0);

	reg = milan_pcie_core_reg(pc, D_IOMMUL1_CTL1);
	val = zen_pcie_core_read(pc, reg);
	val = IOMMUL1_CTL1_SET_ORDERING(val, 1);
	zen_pcie_core_write(pc, reg, val);

	return (0);
}

typedef struct {
	zen_ioms_t *pbc_ioms;
	uint8_t pbc_busoff;
} pci_bus_counter_t;

static int
milan_fabric_hack_bridges_cb(zen_pcie_port_t *port, void *arg)
{
	uint8_t bus, secbus;
	pci_bus_counter_t *pbc = arg;
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;

	bus = ioms->zio_pci_busno;
	if (pbc->pbc_ioms != ioms) {
		pbc->pbc_ioms = ioms;
		pbc->pbc_busoff = 1 + ARRAY_SIZE(milan_int_ports);
		for (uint_t i = 0; i < ARRAY_SIZE(milan_int_ports); i++) {
			const zen_pcie_port_info_t *info =
			    &milan_int_ports[i];
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_PRIBUS, bus);
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_SECBUS, bus + 1 + i);
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_SUBBUS, bus + 1 + i);

		}
	}

	if ((port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN) != 0) {
		return (0);
	}

	secbus = bus + pbc->pbc_busoff;

	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_PRIBUS, bus);
	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_SECBUS, secbus);
	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_SUBBUS, secbus);

	pbc->pbc_busoff++;
	return (0);
}

/*
 * XXX This whole function exists to workaround deficiencies in software and
 * basically try to ape parts of the PCI firmware spec. The OS should natively
 * handle this. In particular, we currently do the following:
 *
 *   o Program a single downstream bus onto each root port. We can only get away
 *     with this because we know there are no other bridges right now. This
 *     cannot be a long term solution, though I know we will be temped to make
 *     it one. I'm sorry future us.
 */
static void
milan_fabric_hack_bridges(zen_fabric_t *fabric)
{
	pci_bus_counter_t c;
	bzero(&c, sizeof (c));

	zen_fabric_walk_pcie_port(fabric, milan_fabric_hack_bridges_cb, &c);
}

/*
 * If this assertion fails, fix the definition in dxio_impl.h or increase the
 * size of the contiguous mapping below.
 */
CTASSERT(sizeof (smu_hotplug_table_t) <= MMU_PAGESIZE);

/*
 * Allocate and initialize the hotplug table. The return value here is used to
 * indicate whether or not the platform has hotplug and thus should continue or
 * not with actual set up.
 */
static bool
milan_smu_hotplug_data_init(zen_fabric_t *fabric)
{
	ddi_dma_attr_t attr;
	milan_fabric_t *mfabric = fabric->zf_uarch_fabric;
	milan_hotplug_t *hp = &mfabric->mf_hotplug;
	const smu_hotplug_entry_t *entry;
	pfn_t pfn;
	bool cont;

	zen_fabric_dma_attr(&attr);
	hp->mh_alloc_len = MMU_PAGESIZE;
	hp->mh_table = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(hp->mh_table, MMU_PAGESIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)hp->mh_table);
	hp->mh_pa = mmu_ptob((uint64_t)pfn);

	if (oxide_board_data->obd_board == OXIDE_BOARD_ETHANOLX) {
		entry = ethanolx_hotplug_ents;
	} else {
		entry = gimlet_hotplug_ents;
	}

	cont = entry[0].se_slotno != SMU_HOTPLUG_ENT_LAST;

	/*
	 * The way the SMU takes this data table is that entries are indexed by
	 * physical slot number. We basically use an interim structure that's
	 * different so we can have a sparse table. In addition, if we find a
	 * device, update that info on its port.
	 */
	for (uint_t i = 0; entry[i].se_slotno != SMU_HOTPLUG_ENT_LAST; i++) {
		uint_t slot = entry[i].se_slotno;
		const smu_hotplug_map_t *map;
		zen_iodie_t *iodie;
		zen_ioms_t *ioms;
		zen_pcie_core_t *pc;
		zen_pcie_port_t *port;
		milan_pcie_port_t *mport;

		hp->mh_table->smt_map[slot] = entry[i].se_map;
		hp->mh_table->smt_func[slot] = entry[i].se_func;
		hp->mh_table->smt_reset[slot] = entry[i].se_reset;

		/*
		 * Attempt to find the port this corresponds to. It should
		 * already have been mapped.
		 */
		map = &entry[i].se_map;
		iodie = &fabric->zf_socs[map->shm_die_id].zs_iodies[0];
		ioms = &iodie->zi_ioms[map->shm_tile_id % 4];
		pc = &ioms->zio_pcie_cores[map->shm_tile_id / 4];
		port = &pc->zpc_ports[map->shm_port_id];
		mport = port->zpp_uarch_pcie_port;

		cmn_err(CE_CONT, "?SMUHP: mapped entry %u to port %p\n",
		    i, port);
		VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0);
		VERIFY0(port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN);
		port->zpp_flags |= ZEN_PCIE_PORT_F_HOTPLUG;
		port->zpp_hp_type = map->shm_format;
		mport->mpp_hp_slotno = slot;
		mport->mpp_hp_smu_mask = entry[i].se_func.shf_mask;

		/*
		 * Calculate any information that can be derived from the port
		 * information.
		 */
		hp->mh_table->smt_map[slot].shm_bridge = pc->zpc_coreno *
		    MILAN_PCIE_CORE_MAX_PORTS + port->zpp_portno;
	}

	return (cont);
}

/*
 * Determine the set of feature bits that should be enabled. If this is Ethanol,
 * use our hacky static versions for a moment.
 */
static uint32_t
milan_hotplug_bridge_features(zen_pcie_port_t *port)
{
	milan_pcie_port_t *mport = port->zpp_uarch_pcie_port;
	uint32_t feats;

	if (oxide_board_data->obd_board == OXIDE_BOARD_ETHANOLX) {
		if (port->zpp_hp_type == ZEN_HP_ENTERPRISE_SSD) {
			return (ethanolx_pcie_slot_cap_entssd);
		} else {
			return (ethanolx_pcie_slot_cap_express);
		}
	}

	feats = PCIE_SLOTCAP_HP_SURPRISE | PCIE_SLOTCAP_HP_CAPABLE;

	/*
	 * The set of features we enable changes based on the type of hotplug
	 * mode. While Enterprise SSD uses a static set of features, the various
	 * ExpressModule modes have a mask register that is used to tell the SMU
	 * that it doesn't support a given feature. As such, we check for these
	 * masks to determine what to enable. Because these bits are used to
	 * turn off features in the SMU, we check for the absence of it (e.g. ==
	 * 0) to indicate that we should enable the feature.
	 */
	switch (port->zpp_hp_type) {
	case ZEN_HP_ENTERPRISE_SSD:
		/*
		 * For Enterprise SSD the set of features that are supported are
		 * considered a constant and this doesn't really vary based on
		 * the board. There is no power control, just surprise hotplug
		 * capabilities. Apparently in this mode there is no SMU command
		 * completion.
		 */
		return (feats | PCIE_SLOTCAP_NO_CMD_COMP_SUPP);
	case ZEN_HP_EXPRESS_MODULE_A:
		if ((mport->mpp_hp_smu_mask & SMU_ENTA_ATTNSW) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_BUTTON;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTA_EMILS) == 0 ||
		    (mport->mpp_hp_smu_mask & SMU_ENTA_EMIL) == 0) {
			feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTA_PWREN) == 0) {
			feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTA_ATTNLED) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTA_PWRLED) == 0) {
			feats |= PCIE_SLOTCAP_PWR_INDICATOR;
		}
		break;
	case ZEN_HP_EXPRESS_MODULE_B:
		if ((mport->mpp_hp_smu_mask & SMU_ENTB_ATTNSW) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_BUTTON;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTB_EMILS) == 0 ||
		    (mport->mpp_hp_smu_mask & SMU_ENTB_EMIL) == 0) {
			feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTB_PWREN) == 0) {
			feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTB_ATTNLED) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
		}

		if ((mport->mpp_hp_smu_mask & SMU_ENTB_PWRLED) == 0) {
			feats |= PCIE_SLOTCAP_PWR_INDICATOR;
		}
		break;
	default:
		return (0);
	}

	return (feats);
}

/*
 * At this point we have finished telling the SMU and its hotplug system to get
 * started. In particular, there are a few things that we do to try and
 * synchronize the PCIe slot and the SMU state, because they are not the same.
 * In particular, we have reason to believe that without a write to the slot
 * control register, the SMU will not write to the GPIO expander and therefore
 * all the outputs will remain at their hardware device's default. The most
 * important part of this is to ensure that we put the slot's power into a
 * defined state.
 */
static int
milan_hotplug_bridge_post_start(zen_pcie_port_t *port, void *arg)
{
	uint16_t ctl, sts;
	uint32_t cap;
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;

	/*
	 * If there is no hotplug support we don't do anything here today. We
	 * assume that if we're in the simple presence mode then we still need
	 * to come through here because in theory the presence changed
	 * indicators should work.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) == 0) {
		return (0);
	}

	sts = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, MILAN_BRIDGE_R_PCI_SLOT_STS);
	cap = pci_getl_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, MILAN_BRIDGE_R_PCI_SLOT_CAP);

	/*
	 * At this point, surprisingly enough, it is expected that all the
	 * notification and fault detection bits be turned on at the SMU as part
	 * of turning on and off the slot. This is a little surprising. Power
	 * was one thing, but at this point it expects to have hotplug
	 * interrupts enabled and all the rest of the features that the hardware
	 * supports (e.g. no MRL sensor changed). Note, we have explicitly left
	 * out turning on the power indicator for present devices.
	 *
	 * Some of the flags need to be conditionally set based on whether or
	 * not they are actually present. We can't turn on the attention button
	 * if there is none. However, others there is no means for software to
	 * discover if they are present or not. So even though we know more and
	 * that say the power fault detection will never work if you've used
	 * Enterprise SSD (or even ExpressModule based on our masks), we set
	 * them anyways, because software will anyways and it helps get the SMU
	 * into a "reasonable" state.
	 */
	ctl = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, MILAN_BRIDGE_R_PCI_SLOT_CTL);
	if ((cap & PCIE_SLOTCAP_ATTN_BUTTON) != 0) {
		ctl |= PCIE_SLOTCTL_ATTN_BTN_EN;
	}

	ctl |= PCIE_SLOTCTL_PWR_FAULT_EN;
	ctl |= PCIE_SLOTCTL_PRESENCE_CHANGE_EN;
	ctl |= PCIE_SLOTCTL_HP_INTR_EN;

	/*
	 * Finally we need to initialize the power state based on slot presence
	 * at this time. Reminder: slot power is enabled when the bit is zero.
	 * It is possible that this may still be creating a race downstream of
	 * this, but in that case, that'll be on the pcieb hotplug logic rather
	 * than us to set up that world here. Only do this if there actually is
	 * a power controller.
	 */
	if ((cap & PCIE_SLOTCAP_POWER_CONTROLLER) != 0) {
		if ((sts & PCIE_SLOTSTS_PRESENCE_DETECTED) != 0) {
			ctl &= ~PCIE_SLOTCTL_PWR_CONTROL;
		} else {
			ctl |= PCIE_SLOTCTL_PWR_CONTROL;
		}
	}
	pci_putw_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, MILAN_BRIDGE_R_PCI_SLOT_CTL, ctl);

	return (0);
}

/*
 * At this point we need to go through and prep all hotplug-capable bridges.
 * This means setting up the following:
 *
 *   o Setting the appropriate slot capabilities.
 *   o Setting the slot's actual number in PCIe and in a secondary SMN location.
 *   o Setting control bits in the PCIe IP to ensure we don't enter loopback
 *     mode and some amount of other state machine control.
 *   o Making sure that power faults work.
 */
static int
milan_hotplug_port_init(zen_pcie_port_t *port, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	uint32_t slot_mask;
	milan_pcie_port_t *mport = port->zpp_uarch_pcie_port;
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;

	/*
	 * Skip over all non-hotplug slots and the simple presence mode. Though
	 * one has to ask oneself, why have hotplug if you're going to use the
	 * simple presence mode.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) == 0 ||
	    port->zpp_hp_type == ZEN_HP_PRESENCE_DETECT) {
		return (0);
	}

	/*
	 * Set the hotplug slot information in the PCIe IP, presumably so that
	 * it'll do something useful for the SMU.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_HP_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_HP_CTL_SET_SLOT(val, mport->mpp_hp_slotno);
	val = PCIE_PORT_HP_CTL_SET_ACTIVE(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * This register is apparently set to ensure that we don't remain in the
	 * detect state machine state.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_CTL5);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL5_SET_WAIT_DETECT(val, 0);
	zen_pcie_port_write(port, reg, val);

	/*
	 * This bit is documented to cause the LC to disregard most training
	 * control bits in received TS1 and TS2 ordered sets.  Training control
	 * bits include Compliance Receive, Hot Reset, Link Disable, Loopback,
	 * and Disable Scrambling.  As all our ports are Downstream Ports, we
	 * are required to ignore most of these; the PCIe standard still
	 * requires us to act on Compliance Receive and the PPR implies that we
	 * do even if this bit is set (the other four are listed as being
	 * ignored).
	 *
	 * However... an AMD firmware bug for which we have no additional
	 * information implies that this does more than merely ignore training
	 * bits in received TSx, and also makes the Secondary Bus Reset bit in
	 * the Bridge Control register not work or work incorrectly.  That is,
	 * there may be a hardware bug that causes this bit to have unintended
	 * and undocumented side effects that also violate the standard.  In our
	 * case, we're going to set this anyway, because there is nothing
	 * anywhere in illumos that uses the Secondary Bus Reset feature and it
	 * seems much more important to be sure that our downstream ports can't
	 * be disabled or otherwise affected by a misbehaving or malicious
	 * downstream device that might set some of these bits.
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_TRAINBITS_DIS(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure that power faults can actually work (in theory).
	 */
	reg = milan_pcie_port_reg(port, D_PCIE_PORT_PCTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_PCTL_SET_PWRFLT_EN(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Go through and set up the slot capabilities register. In our case
	 * we've already filtered out the non-hotplug capable bridges. To
	 * determine the set of hotplug features that should be set here we
	 * derive that from the actual hoptlug entities. Because one is required
	 * to give the SMU a list of functions to mask, the unmasked bits tells
	 * us what to enable as features here.
	 */
	slot_mask = PCIE_SLOTCAP_ATTN_BUTTON | PCIE_SLOTCAP_POWER_CONTROLLER |
	    PCIE_SLOTCAP_MRL_SENSOR | PCIE_SLOTCAP_ATTN_INDICATOR |
	    PCIE_SLOTCAP_PWR_INDICATOR | PCIE_SLOTCAP_HP_SURPRISE |
	    PCIE_SLOTCAP_HP_CAPABLE | PCIE_SLOTCAP_EMI_LOCK_PRESENT |
	    PCIE_SLOTCAP_NO_CMD_COMP_SUPP;

	val = pci_getl_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, MILAN_BRIDGE_R_PCI_SLOT_CAP);
	val &= ~(PCIE_SLOTCAP_PHY_SLOT_NUM_MASK <<
	    PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT);
	val |= mport->mpp_hp_slotno << PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT;
	val &= ~slot_mask;
	val |= milan_hotplug_bridge_features(port);
	pci_putl_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, MILAN_BRIDGE_R_PCI_SLOT_CAP, val);

	/*
	 * Finally we need to go through and unblock training now that we've set
	 * everything else on the slot. Note, this is done before we tell the
	 * SMU about hotplug configuration, so strictly speaking devices will
	 * unlikely start suddenly training: PERST is still asserted to them on
	 * boards where that's under GPIO network control.
	 */
	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_SWRST_CTL6);
	val = zen_pcie_core_read(pc, reg);
	val = bitset32(val, port->zpp_portno, port->zpp_portno, 0);
	zen_pcie_core_write(pc, reg, val);

	return (0);
}

/*
 * This is an analogue to the above functions; however, it operates on the PCIe
 * core basis rather than the individual port or bridge. This mostly includes:
 *
 *   o Making sure that there are no holds on link training on any port.
 *   o Ensuring that presence detection is based on an 'OR'
 */
static int
milan_hotplug_core_init(zen_pcie_core_t *pc, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Nothing to do if there's no hotplug.
	 */
	if ((pc->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG) == 0) {
		return (0);
	}

	reg = milan_pcie_core_reg(pc, D_PCIE_CORE_PRES);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PRES_SET_MODE(val, PCIE_CORE_PRES_MODE_OR);
	zen_pcie_core_write(pc, reg, val);

	return (0);
}

/*
 * Begin the process of initializing the hotplug subsystem with the SMU. In
 * particular we need to do the following steps:
 *
 *  o Send a series of commands to set up the i2c switches in general. These
 *    correspond to the various bit patterns that we program in the function
 *    payload.
 *
 *  o Set up and send across our hotplug table.
 *
 *  o Finish setting up the bridges to be ready for hotplug.
 *
 *  o Actually tell it to start.
 *
 * Unlike with DXIO initialization, it appears that hotplug initialization only
 * takes place on the primary SMU. In some ways, this makes some sense because
 * the hotplug table has information about which dies and sockets are used for
 * what and further, only the first socket ever is connected to the hotplug i2c
 * bus; however, it is still also a bit mysterious.
 */
static bool
milan_hotplug_init(zen_fabric_t *fabric)
{
	milan_fabric_t *mfabric = fabric->zf_uarch_fabric;
	milan_hotplug_t *hp = &mfabric->mf_hotplug;
	zen_iodie_t *iodie = &fabric->zf_socs[0].zs_iodies[0];

	/*
	 * These represent the addresses that we need to program in the SMU.
	 * Strictly speaking, the lower 8-bits represents the addresses that the
	 * SMU seems to expect. The upper byte is a bit more of a mystery;
	 * however, it does correspond to the expected values that AMD roughly
	 * documents for 5-bit bus segment value which is the shf_i2c_bus member
	 * of the smu_hotplug_function_t.
	 */
	const uint32_t i2c_addrs[4] = { 0x70, 0x171, 0x272, 0x373 };

	if (!milan_smu_hotplug_data_init(fabric)) {
		/*
		 * This case is used to indicate that there was nothing in
		 * particular that needed hotplug. Therefore, we don't bother
		 * trying to tell the SMU about it.
		 */
		return (true);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(i2c_addrs); i++) {
		if (!milan_smu_rpc_i2c_switch(iodie, i2c_addrs[i])) {
			return (false);
		}
	}

	if (!milan_smu_rpc_give_address(iodie, MSAK_HOTPLUG, hp->mh_pa)) {
		return (false);
	}

	if (!milan_smu_rpc_send_hotplug_table(iodie)) {
		return (false);
	}

	/*
	 * Go through now and set up bridges for hotplug data. Honor the spirit
	 * of the old world by doing this after we send the hotplug table, but
	 * before we enable things. It's unclear if the order is load bearing or
	 * not.
	 */
	(void) zen_fabric_walk_pcie_core(fabric, milan_hotplug_core_init, NULL);
	(void) zen_fabric_walk_pcie_port(fabric, milan_hotplug_port_init, NULL);

	if (!milan_smu_rpc_hotplug_flags(iodie, 0)) {
		return (false);
	}

	/*
	 * This is an unfortunate bit. The SMU relies on someone else to have
	 * set the actual state of the i2c clock.
	 */
	if (!milan_fixup_i2c_clock()) {
		return (false);
	}

	if (!milan_smu_rpc_start_hotplug(iodie, false, 0)) {
		return (false);
	}

	/*
	 * Now that this is done, we need to go back through and do some final
	 * pieces of slot initialization which are probably necessary to get the
	 * SMU into the same place as we are with everything else.
	 */
	(void) zen_fabric_walk_pcie_port(fabric,
	    milan_hotplug_bridge_post_start, NULL);

	return (true);
}

/*
 * Do everything else required to finish configuring the nBIF and get the PCIe
 * engine up and running.
 */
void
milan_fabric_pcie(zen_fabric_t *fabric)
{
	/*
	 * Currently we do all of our initial DXIO training for PCIe before we
	 * enable features that have to do with the SMU. XXX Cargo Culting.
	 */

	/*
	 * It's time to begin the dxio initialization process. We do this in a
	 * few different steps:
	 *
	 *   1. Program all of the misc. settings and variables that it wants
	 *	before we begin to load data anywhere.
	 *   2. Construct the per-die payloads that we require and assemble
	 *	them.
	 *   3. Actually program all of the different payloads we need.
	 *   4. Go back and set a bunch more things that probably can all be
	 *	done in (1) when we're done aping.
	 *   5. Make the appropriate sacrifice to the link training gods.
	 *   6. Kick off and process the state machines, one I/O die at a time.
	 *
	 * XXX htf do we want to handle errors
	 */
	zen_pcie_populate_dbg(fabric, MPCS_PRE_DXIO_INIT,
	    ZEN_IODIE_MATCH_ANY);
	if (zen_fabric_walk_iodie(fabric, milan_dxio_init, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: lasciate ogni "
		    "speranza voi che pcie");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, milan_dxio_plat_data, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: no platform "
		    "data");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, milan_dxio_load_data, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to load "
		    "data into dxio");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, milan_dxio_more_conf, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to do yet "
		    "more configuration");
		return;
	}

	zen_pcie_populate_dbg(fabric, MPCS_DXIO_SM_START,
	    ZEN_IODIE_MATCH_ANY);
	if (zen_fabric_walk_iodie(fabric, milan_dxio_state_machine, NULL) !=
	    0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to walk "
		    "through the state machine");
		return;
	}

	cmn_err(CE_CONT, "?DXIO LISM execution completed successfully\n");

	/*
	 * Now that we have successfully trained devices, it's time to go
	 * through and set up the bridges so that way we can actual handle them
	 * aborting transactions and related.
	 */
	zen_fabric_walk_pcie_core(fabric, milan_fabric_init_pcie_core, NULL);
	zen_fabric_walk_pcie_port(fabric, milan_fabric_init_bridges, NULL);

	/*
	 * XXX This is a terrible hack. We should really fix pci_boot.c and we
	 * better before we go to market.
	 */
	milan_fabric_hack_bridges(fabric);

	/*
	 * At this point, go talk to the SMU to actually initialize our hotplug
	 * support.
	 */
	zen_pcie_populate_dbg(fabric, MPCS_PRE_HOTPLUG, ZEN_IODIE_MATCH_ANY);
	if (!milan_hotplug_init(fabric)) {
		cmn_err(CE_WARN, "SMUHP: initialisation failed; PCIe hotplug "
		    "may not function properly");
	}

	zen_pcie_populate_dbg(fabric, MPCS_POST_HOTPLUG, ZEN_IODIE_MATCH_ANY);

	/*
	 * XXX At some point, maybe not here, but before we really go too much
	 * futher we should lock all the various MMIO assignment registers,
	 * especially ones we don't intend to use.
	 */
}
