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
 * AMD Zen's I/O fabric. This consists of both the data fabric and the
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
 *                 \-- zen_core_t (qty varies 4-8)
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
 * SMU RPC function; see zen_dxio_rpc().  These form a critical mechanism for
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
 * MAPPED - DXIO engine configuration (see zen_dxio_data.c) describing each
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
 * zen_fabric_init_pcie_straps()) are nothing like this.  First, all of the
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
 * - MILAN_IOPORT_COMPAT_SIZE ports beginning at 0 for legacy I/O
 * - MILAN_COMPAT_MMIO_SIZE bytes beginning at MILAN_PHYSADDR_COMPAT_MMIO for
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
 * zen_xx_allocate() for the implementation of these allocations/reservations.
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
#include <sys/amdzen/fch.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/fch/smi.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/nbif_impl.h>
#if 0
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/hacks.h>
#include <sys/io/milan/ioapic.h>
#include <sys/io/milan/iohc.h>
#include <sys/io/milan/iommu.h>
#include <sys/io/milan/nbif.h>
#include <sys/io/milan/nbif_impl.h>
#include <sys/io/milan/pcie_rsmu.h>
#include <sys/io/milan/smu_impl.h>
#endif

#include <asm/bitmap.h>

#include <sys/amdzen/df.h>

zen_fabric_t *zen_fabric;

#if 0
#include <milan/milan_apob.h>
#include <milan/milan_physaddrs.h>
#endif

/*
 * XXX This header contains a lot of the definitions that the broader system is
 * currently using for register definitions. For the moment we're trying to keep
 * this consolidated, hence this wacky include path.
 */
#include <io/amdzen/amdzen.h>

typedef struct zen_pcie_port_info {
	uint8_t	zppi_dev;
	uint8_t	zppi_func;
} zen_pcie_port_info_t;

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently following the recommendation of the PPR.
 */
typedef struct zen_ioapic_info {
	uint8_t zii_group;
	uint8_t zii_swiz;
	uint8_t zii_map;
} zen_ioapic_info_t;

/* XXX Track platform default presence */
typedef struct zen_nbif_info {
	zen_nbif_func_type_t	zni_type;
	uint8_t			zni_dev;
	uint8_t			zni_func;
} zen_nbif_info_t;

/*
 * This structure and the following table encodes the mapping of the set of dxio
 * lanes to a given PCIe core on an IOMS. This is ordered such that all of the
 * normal engines are present; however, the wafl core, being special is not
 * here. The dxio engine uses different lane numbers than the phys. Note, that
 * all lanes here are inclusive. e.g. [start, end].
 */
typedef struct zen_pcie_core_info {
	const char	*zpci_name;
	uint16_t	zpci_dxio_start;
	uint16_t	zpci_dxio_end;
	uint16_t	zpci_phy_start;
	uint16_t	zpci_phy_end;
} zen_pcie_core_info_t;

extern void *contig_alloc(size_t, ddi_dma_attr_t *, uintptr_t, int);
extern void contig_free(void *, size_t);

static boolean_t zen_smu_rpc_read_brand_string(zen_iodie_t *,
    char *, size_t);

typedef int (*zen_iodie_cb_f)(zen_iodie_t *, void *);
typedef int (*zen_ioms_cb_f)(zen_ioms_t *, void *);
typedef int (*zen_pcie_core_cb_f)(zen_pcie_core_t *, void *);
typedef int (*zen_pcie_port_cb_f)(zen_pcie_port_t *, void *);
typedef int (*zen_ccd_cb_f)(zen_ccd_t *, void *);
typedef int (*zen_ccx_cb_f)(zen_ccx_t *, void *);
typedef int (*zen_core_cb_f)(zen_core_t *, void *);

/*
 * Variable to let us dump all SMN traffic while still developing.
 */
static int
zen_fabric_walk_iodie(zen_fabric_t *fabric, zen_iodie_cb_f func, void *arg)
{
	for (uint_t socno = 0; socno < fabric->zf_nsocs; socno++) {
		zen_soc_t *soc = &fabric->zf_socs[socno];
		for (uint_t iono = 0; iono < soc->zs_ndies; iono++) {
			int ret;
			zen_iodie_t *iodie = &soc->zs_iodies[iono];

			ret = func(iodie, arg);
			if (ret != 0) {
				return (ret);
			}
		}
	}

	return (0);
}

int
zen_walk_iodie(zen_iodie_cb_f func, void *arg)
{
	return (zen_fabric_walk_iodie(zen_fabric, func, arg));
}

typedef struct zen_fabric_ioms_cb {
	zen_ioms_cb_f	zfic_func;
	void		*zfic_arg;
} zen_fabric_ioms_cb_t;

static int
zen_fabric_walk_ioms_iodie_cb(zen_iodie_t *iodie, void *arg)
{
	const zen_fabric_ioms_cb_t *cb =
	    (const zen_fabric_ioms_cb_t *)arg;
	for (uint_t iomsno = 0; iomsno < iodie->zi_nioms; iomsno++) {
		zen_ioms_t *ioms = &iodie->zi_ioms[iomsno];
		int ret = cb->zfic_func(ioms, cb->zfic_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_ioms(zen_fabric_t *fabric, zen_ioms_cb_f func, void *arg)
{
	zen_fabric_ioms_cb_t cb = {
	    .zfic_func = func,
	    .zfic_arg = arg,
	};

	return (zen_fabric_walk_iodie(fabric, zen_fabric_walk_ioms_iodie_cb,
	    &cb));
}

int
zen_walk_ioms(zen_ioms_cb_f func, void *arg)
{
	return (zen_fabric_walk_ioms(zen_fabric, func, arg));
}

typedef struct zen_fabric_nbif_cb {
	zen_nbif_cb_f	zfnc_func;
	void		*zfnc_arg;
} zen_fabric_nbif_cb_t;

static int
zen_fabric_walk_nbif_ioms_cb(zen_ioms_t *ioms, void *arg)
{
	const zen_fabric_nbif_cb_t *cb = (const zen_fabric_nbif_cb_t *)arg;
	for (uint_t nbifno = 0; nbifno < ioms->zio_nnbifs; nbifno++) {
		zen_nbif_t *nbif = &ioms->zio_nbifs[nbifno];
		int ret = cb->zfnc_func(nbif, cb->zfnc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_nbif(zen_fabric_t *fabric, zen_nbif_cb_f func, void *arg)
{
	zen_fabric_nbif_cb_t cb = {
	    .zfnc_func = func,
	    .zfnc_arg = arg,
	};

	return (zen_fabric_walk_ioms(fabric, zen_fabric_walk_nbif_ioms_cb,
	    &cb));
}

typedef struct zen_fabric_pcie_core_cb {
	zen_pcie_core_cb_f	zfpcc_func;
	void			*zfpcc_arg;
} zen_fabric_pcie_core_cb_t;

static int
zen_fabric_walk_pcie_core_cb(zen_ioms_t *ioms, void *arg)
{
	const zen_fabric_pcie_core_cb_t *cb =
	    (const zen_fabric_pcie_core_cb_t *)arg;
	for (uint_t pcno = 0; pcno < ioms->zio_npcie_cores; pcno++) {
		zen_pcie_core_t *pc = &ioms->zio_pcie_cores[pcno];
		int ret = cb->zfpcc_func(pc, cb->zfpcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_pcie_core(zen_fabric_t *fabric, zen_pcie_core_cb_f func,
    void *arg)
{
	zen_fabric_pcie_core_cb_t cb = {
	    .zfpcc_func = func,
	    .zfpcc_arg = arg,
	};

	return (zen_fabric_walk_ioms(fabric, zen_fabric_walk_pcie_core_cb,
	    &cb));
}

typedef struct zen_fabric_pcie_port_cb {
	zen_pcie_port_cb_f	zfppc_func;
	void			*zfppc_arg;
} zen_fabric_pcie_port_cb_t;

static int
zen_fabric_walk_pcie_port_cb(zen_pcie_core_t *pc, void *arg)
{
	zen_fabric_pcie_port_cb_t *cb = (zen_fabric_pcie_port_cb_t *)arg;

	for (uint_t portno = 0; portno < pc->zpc_nports; portno++) {
		zen_pcie_port_t *port = &pc->zpc_ports[portno];
		int ret = cb->zfppc_func(port, cb->zfppc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_pcie_port(zen_fabric_t *fabric, zen_pcie_port_cb_f func,
    void *arg)
{
	zen_fabric_pcie_port_cb_t cb = {
	    .zfppc_func = func,
	    .zfppc_arg = arg,
	};

	return (zen_fabric_walk_pcie_core(fabric, zen_fabric_walk_pcie_port_cb,
	    &cb));
}

typedef struct zen_fabric_ccd_cb {
	zen_ccd_cb_f	zfcc_func;
	void		*zfcc_arg;
} zen_fabric_ccd_cb_t;

static int
zen_fabric_walk_ccd_iodie_cb(zen_iodie_t *iodie, void *arg)
{
	const zen_fabric_ccd_cb_t *cb = (const zen_fabric_ccd_cb_t *)arg;

	for (uint8_t ccdno = 0; ccdno < iodie->zi_nccds; ccdno++) {
		zen_ccd_t *ccd = &iodie->zi_ccds[ccdno];
		int ret = cb->zfcc_func(ccd, cb->zfcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_ccd(zen_fabric_t *fabric, zen_ccd_cb_f func, void *arg)
{
	zen_fabric_ccd_cb_t cb = {
	    .zfcc_func = func,
	    .zfcc_arg = arg,
	};

	return (zen_fabric_walk_iodie(fabric, zen_fabric_walk_ccd_iodie_cb,
	    &cb));
}

typedef struct zen_fabric_ccx_cb {
	zen_ccx_cb_f	zfcc_func;
	void		*zfcc_arg;
} zen_fabric_ccx_cb_t;

static int
zen_fabric_walk_ccx_ccd_cb(zen_ccd_t *ccd, void *arg)
{
	const zen_fabric_ccx_cb_t *cb = (const zen_fabric_ccx_cb_t *)arg;

	for (uint8_t ccxno = 0; ccxno < ccd->zcd_nccxs; ccxno++) {
		zen_ccx_t *ccx = &ccd->zcd_ccxs[ccxno];
		int ret = cb->zfcc_func(ccx, cb->zfcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_ccx(zen_fabric_t *fabric, zen_ccx_cb_f func, void *arg)
{
	zen_fabric_ccx_cb_t cb = {
	    .zfcc_func = func,
	    .zfcc_arg = arg,
	};

	return (zen_fabric_walk_ccd(fabric, zen_fabric_walk_ccx_ccd_cb, &cb));
}

typedef struct zen_fabric_core_cb {
	zen_core_cb_f	zfcc_func;
	void		*zfcc_arg;
} zen_fabric_core_cb_t;

static int
zen_fabric_walk_core_ccx_cb(zen_ccx_t *ccx, void *arg)
{
	const zen_fabric_core_cb_t *cb = (const zen_fabric_core_cb_t *)arg;

	for (uint8_t coreno = 0; coreno < ccx->zcx_ncores; coreno++) {
		zen_core_t *core = &ccx->zcx_cores[coreno];
		int ret = cb->zfcc_func(core, cb->zfcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_core(zen_fabric_t *fabric, zen_core_cb_f func, void *arg)
{
	zen_fabric_core_cb_t cb = {
	    .zfcc_func = func,
	    .zfcc_arg = arg,
	};

	return (zen_fabric_walk_ccx(fabric, zen_fabric_walk_core_ccx_cb, &cb));
}

typedef struct zen_fabric_thread_cb {
	zen_thread_cb_f		zftc_func;
	void			*zftc_arg;
} zen_fabric_thread_cb_t;

static int
zen_fabric_walk_thread_core_cb(zen_core_t *core, void *arg)
{
	zen_fabric_thread_cb_t *cb = (zen_fabric_thread_cb_t *)arg;

	for (uint8_t threadno = 0; threadno < core->zc_nthreads; threadno++) {
		zen_thread_t *thread = &core->zc_threads[threadno];
		int ret = cb->zftc_func(thread, cb->zftc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_thread(zen_fabric_t *fabric, zen_thread_cb_f func, void *arg)
{
	zen_fabric_thread_cb_t cb = {
	    .zftc_func = func,
	    .zftc_arg = arg,
	};

	return (zen_fabric_walk_core(fabric, zen_fabric_walk_thread_core_cb,
	    &cb));
}

int
zen_walk_thread(zen_thread_cb_f func, void *arg)
{
	return (zen_fabric_walk_thread(zen_fabric, func, arg));
}

typedef struct {
	uint32_t	zffi_dest;
	zen_ioms_t	*zffi_ioms;
} zen_fabric_find_ioms_t;

static int
zen_fabric_find_ioms_cb(zen_ioms_t *ioms, void *arg)
{
	zen_fabric_find_ioms_t *zffi = (zen_fabric_find_ioms_t *)arg;

	if (zffi->zffi_dest == ioms->zio_fabric_id) {
		zffi->zffi_ioms = ioms;
		return (1);
	}

	return (0);
}

static int
zen_fabric_find_ioms_by_bus_cb(zen_ioms_t *ioms, void *arg)
{
	zen_fabric_find_ioms_t *zffi = (zen_fabric_find_ioms_t *)arg;

	if (zffi->zffi_dest == ioms->zio_pci_busno) {
		zffi->zffi_ioms = ioms;
		return (1);
	}

	return (0);
}

static zen_ioms_t *
zen_fabric_find_ioms(zen_fabric_t *fabric, uint32_t destid)
{
	zen_fabric_find_ioms_t zffi = {
	    .zffi_dest = destid,
	    .zffi_ioms = NULL,
	};

	(void) zen_fabric_walk_ioms(fabric, zen_fabric_find_ioms_cb,
	    &zffi);

	return (zffi.zffi_ioms);
}

static zen_ioms_t *
zen_fabric_find_ioms_by_bus(zen_fabric_t *fabric, uint32_t pci_bus)
{
	zen_fabric_find_ioms_t zffi = {
	    .zffi_dest = pci_bus,
	    .zffi_ioms = NULL,
	};

	(void) zen_fabric_walk_ioms(fabric, zen_fabric_find_ioms_by_bus_cb,
	    &zffi);

	return (zffi.zffi_ioms);
}

typedef struct zen_fabric_find_pcie_core {
	const zen_iodie_t *zffpc_iodie;
	uint16_t zffpc_start;
	uint16_t zffpc_end;
	zen_pcie_core_t *zffpc_pc;
} zen_fabric_find_pcie_core_t;

static int
zen_fabric_find_pcie_core_by_lanes_cb(zen_pcie_core_t *pc, void *arg)
{
	zen_fabric_find_pcie_core_t *zffpc = (zen_fabric_find_pcie_core_t *)arg;

	if (zffpc->zffpc_iodie != pc->zpc_ioms->zio_iodie &&
	    zffpc->zffpc_start >= pc->zpc_dxio_lane_start &&
	    zffpc->zffpc_start <= pc->zpc_dxio_lane_end &&
	    zffpc->zffpc_end >= pc->zpc_dxio_lane_start &&
	    zffpc->zffpc_end <= pc->zpc_dxio_lane_end) {
		zffpc->zffpc_pc = pc;
		return (1);
	}

	return (0);
}


static zen_pcie_core_t *
zen_fabric_find_pcie_core_by_lanes(zen_iodie_t *iodie,
    uint16_t start, uint16_t end)
{
	ASSERT3U(start, <=, end);

	zen_fabric_find_pcie_core_t zffpc = {
	    .zffpc_iodie = iodie,
	    .zffpc_start = start,
	    .zffpc_end = end,
	    .zffpc_pc = NULL,
	};

	(void) zen_fabric_walk_pcie_core(iodie->zi_soc->zs_fabric,
	    zen_fabric_find_pcie_core_by_lanes_cb, &zffpc);

	return (zffpc.zffpc_pc);
}

typedef struct zen_fabric_find_thread {
	uint32_t	zfft_search;
	uint32_t	zfft_count;
	zen_thread_t	*zfft_found;
} zen_fabric_find_thread_t;

static int
zen_fabric_find_thread_by_cpuid_cb(zen_thread_t *thread, void *arg)
{
	zen_fabric_find_thread_t *zfft = (zen_fabric_find_thread_t *)arg;

	if (zfft->zfft_count == zfft->zfft_search) {
		zfft->zfft_found = thread;
		return (1);
	}
	++zfft->zfft_count;

	return (0);
}

zen_thread_t *
zen_fabric_find_thread_by_cpuid(uint32_t cpuid)
{
	zen_fabric_find_thread_t zfft = {
	    .zfft_search = cpuid,
	    .zfft_count = 0,
	    .zfft_found = NULL,
	};

	(void) zen_fabric_walk_thread(zen_fabric,
	    zen_fabric_find_thread_by_cpuid_cb, &zfft);

	return (zfft.zfft_found);
}

/*
 * Create DMA attributes that are appropriate for the SMU. In particular, we
 * know experimentally that there is usually a 32-bit length register for DMA
 * and generally a 64-bit address register. There aren't many other bits that we
 * actually know here, as such, we generally end up making some assumptions out
 * of paranoia in an attempt at safety. In particular, we assume and ask for
 * page alignment here.
 *
 * XXX Remove 32-bit addr_hi constraint.
 */
static void
zen_smu_dma_attr(ddi_dma_attr_t *attr)
{
	bzero(attr, sizeof (attr));
	attr->dma_attr_version = DMA_ATTR_V0;
	attr->dma_attr_addr_lo = 0;
	attr->dma_attr_addr_hi = UINT32_MAX;
	attr->dma_attr_count_max = UINT32_MAX;
	attr->dma_attr_align = MMU_PAGESIZE;
	attr->dma_attr_minxfer = 1;
	attr->dma_attr_maxxfer = UINT32_MAX;
	attr->dma_attr_seg = UINT32_MAX;
	attr->dma_attr_sgllen = 1;
	attr->dma_attr_granular = 1;
	attr->dma_attr_flags = 0;
}
