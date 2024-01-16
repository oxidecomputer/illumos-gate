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

/*
 * Code to access, initialize, inspect, and manage Genoa's I/O fabric,
 * consisting of the data fabric and northbridges.
 *
 * --------------------------------------
 * Physical Organization and Nomenclature
 * --------------------------------------
 *
 * In AMD's Zen 2, 3, and 4 designs, the CPU socket is organized as a series of
 * chiplets with a series of compute complexes coupled to a central I/O die; see
 * uts/intel/os/cpuid.c has an example.
 *
 * The I/O die is the major device that we are concerned with here as it
 * bridges the cores to the rest of the system through a combination of
 * devices and I/O paths.  The part of the I/O die that we will spend most of
 * our time dealing with is the "northbridge I/O unit", or NBIO.  In DF (Zen
 * data fabric) terms, NBIOs are a class of device called an IOMS (I/O
 * master-slave).  These are represented in our fabric data structures as
 * subordinate to an I/O die.  On Genoa processors, each I/O die has 2 NBIO
 * instances; other processor families have these in differing number or
 * organization.  Since we're interested in Zen 4 here (and since Zen 2 and 3
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
 * genoa_fabric_t (DF -- root)
 * |
 * \-- genoa_soc_t (qty 1 or 2)
 *     |
 *     \-- genoa_iodie_t (qty 1)
 *         |
 *         +-- genoa_ioms_t (qty 4, one per NBIO)
 *         |   |
 *         |   +-- genoa_pcie_core_t (qty 2, except 3 for IOMS 0)
 *         |   |   |
 *         |   |   \-- genoa_pcie_port_t (qty 8, except 2 for IOMS 0 RC 2)
 *         |   |
 *         |   \-- genoa_nbif_t (qty 3 + 2 in "alternate space")
 *         |
 *         \-- genoa_ccd_t (qty varies 1-8)
 *             |
 *             \-- genoa_ccx_t (qty 1)
 *                 |
 *                 \-- genoa_core_t (qty varies 4-8)
 *                     |
 *                     \-- genoa_thread_t (qty 2, unless SMT is disabled)
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
 * SMU RPC function; see genoa_dxio_rpc().  These form a critical mechanism for
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
 * MAPPED - DXIO engine configuration (see genoa_dxio_data.c) describing each
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
 * die (for Genoa, this means everything hanging off the socket for which this
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
 * genoa_fabric_init_pcie_straps()) are nothing like this.  First, all of the
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
 * can be assigned resources via BARs.  The Genoa PPR 13.1.4.4 imposes certain
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
 * - GENOA_IOPORT_COMPAT_SIZE ports beginning at 0 for legacy I/O
 * - GENOA_COMPAT_MMIO_SIZE bytes beginning at GENOA_PHYSADDR_COMPAT_MMIO for
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
 * GENOA_SEC_IOMS_GEN_IO_SPACE is the number of contiguous legacy I/O ports to
 * reserve for non-PCI consumers.  While not currently used, the remote FCH has
 * a unit called the A-Link/B-Link bridge accessed via legacy I/O space at a
 * group of ports programmable via an FCH BAR; to access this, we would need to
 * reserve space routed to the secondary FCH's IOMS, so we try to do that.
 *
 * GENOA_SEC_IOMS_GEN_MMIO32_SPACE is the size in bytes of the contiguous MMIO
 * region below the 32-bit boundary to reserve for non-PCI consumers.
 *
 * GENOA_SEC_IOMS_GEN_MMIO64_SPACE is the corresponding figure for MMIO space
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
 * genoa_xx_allocate() for the implementation of these allocations/reservations.
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
#include <sys/stdbool.h>
#include <sys/ddi.h>
#include <sys/ksynch.h>
#include <sys/pci.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/pcie.h>
#include <sys/spl.h>
#include <sys/debug.h>
#include <sys/prom_debug.h>
#include <sys/x86_archext.h>
#include <sys/bitext.h>
#include <sys/sysmacros.h>
#include <sys/memlist_impl.h>
#include <sys/machsystm.h>
#include <sys/plat/pci_prd.h>
#include <sys/apic.h>
#include <sys/cpuvar.h>
#include <sys/amdzen/fch.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/fch/smi.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/dxio_impl.h>
#include <sys/io/genoa/hacks.h>
#include <sys/io/genoa/ioapic.h>
#include <sys/io/genoa/iohc.h>
#include <sys/io/genoa/iommu.h>
#include <sys/io/genoa/nbif.h>
#include <sys/io/genoa/nbif_impl.h>
#include <sys/io/genoa/pcie.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/io/genoa/pcie_rsmu.h>
#include <sys/io/genoa/smu_impl.h>

#include <asm/bitmap.h>

#include <sys/amdzen/df.h>

#include <genoa/genoa_apob.h>
#include <genoa/genoa_physaddrs.h>

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
typedef struct genoa_dxio_rpc {
	uint32_t	mdr_req;
	uint32_t	mdr_dxio_resp;
	uint32_t	mdr_smu_resp;
	uint32_t	mdr_engine;
	uint32_t	mdr_arg0;
	uint32_t	mdr_arg1;
	uint32_t	mdr_arg2;
	uint32_t	mdr_arg3;
} genoa_dxio_rpc_t;

typedef struct genoa_pcie_port_info {
	uint8_t	mppi_dev;
	uint8_t	mppi_func;
} genoa_pcie_port_info_t;

/*
 * These three tables encode knowledge about how the SoC assigns devices and
 * functions to root ports.
 */
static const genoa_pcie_port_info_t genoa_pcie0[GENOA_PCIE_CORE_MAX_PORTS] = {
	{ 0x1, 0x1 },
	{ 0x1, 0x2 },
	{ 0x1, 0x3 },
	{ 0x1, 0x4 },
	{ 0x1, 0x5 },
	{ 0x1, 0x6 },
	{ 0x1, 0x7 },
	{ 0x2, 0x1 }
};

static const genoa_pcie_port_info_t genoa_pcie1[GENOA_PCIE_CORE_MAX_PORTS] = {
	{ 0x3, 0x1 },
	{ 0x3, 0x2 },
	{ 0x3, 0x3 },
	{ 0x3, 0x4 },
	{ 0x3, 0x5 },
	{ 0x3, 0x6 },
	{ 0x3, 0x7 },
	{ 0x4, 0x1 }
};

static const genoa_pcie_port_info_t genoa_pcie2[GENOA_PCIE_CORE_WAFL_NPORTS] = {
	{ 0x5, 0x1 },
	{ 0x5, 0x2 }
};

/*
 * These are internal bridges that correspond to NBIFs; they are modeled as
 * ports but there is no physical port brought out of the package.
 */
static const genoa_pcie_port_info_t genoa_int_ports[4] = {
	{ 0x7, 0x1 },
	{ 0x8, 0x1 },
	{ 0x8, 0x2 },
	{ 0x8, 0x3 }
};

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently following the recommendation of the PPR.
 */
typedef struct genoa_ioapic_info {
	uint8_t mii_group;
	uint8_t mii_swiz;
	uint8_t mii_map;
} genoa_ioapic_info_t;

static const genoa_ioapic_info_t genoa_ioapic_routes[IOAPIC_NROUTES] = {
	{ .mii_group = 0x0, .mii_map = 0x10,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x1, .mii_map = 0x11,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x2, .mii_map = 0x12,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x3, .mii_map = 0x13,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x4, .mii_map = 0x10,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x5, .mii_map = 0x11,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x6, .mii_map = 0x12,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x7, .mii_map = 0x13,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x7, .mii_map = 0x0c,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x6, .mii_map = 0x0d,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x5, .mii_map = 0x0e,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x4, .mii_map = 0x0f,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x3, .mii_map = 0x0c,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x2, .mii_map = 0x0d,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x1, .mii_map = 0x0e,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x0, .mii_map = 0x0f,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x0, .mii_map = 0x08,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x1, .mii_map = 0x09,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x2, .mii_map = 0x0a,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x3, .mii_map = 0x0b,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x4, .mii_map = 0x08,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x5, .mii_map = 0x09,
	    .mii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC }
};

/* XXX Track platform default presence */
typedef struct genoa_nbif_info {
	genoa_nbif_func_type_t	mni_type;
	uint8_t			mni_dev;
	uint8_t			mni_func;
} genoa_nbif_info_t;

static const genoa_nbif_info_t genoa_nbif0[GENOA_NBIF0_NFUNCS] = {
	{ .mni_type = GENOA_NBIF_T_DUMMY, .mni_dev = 0, .mni_func = 0 },
	{ .mni_type = GENOA_NBIF_T_NTB, .mni_dev = 0, .mni_func = 1 },
	{ .mni_type = GENOA_NBIF_T_PTDMA, .mni_dev = 0, .mni_func = 2 }
};

static const genoa_nbif_info_t genoa_nbif1[GENOA_NBIF1_NFUNCS] = {
	{ .mni_type = GENOA_NBIF_T_DUMMY, .mni_dev = 0, .mni_func = 0 },
	{ .mni_type = GENOA_NBIF_T_PSPCCP, .mni_dev = 0, .mni_func = 1 },
	{ .mni_type = GENOA_NBIF_T_PTDMA, .mni_dev = 0, .mni_func = 2 },
	{ .mni_type = GENOA_NBIF_T_USB, .mni_dev = 0, .mni_func = 3 },
	{ .mni_type = GENOA_NBIF_T_AZ, .mni_dev = 0, .mni_func = 4 },
	{ .mni_type = GENOA_NBIF_T_SATA, .mni_dev = 1, .mni_func = 0 },
	{ .mni_type = GENOA_NBIF_T_SATA, .mni_dev = 2, .mni_func = 0 }
};

static const genoa_nbif_info_t genoa_nbif2[GENOA_NBIF2_NFUNCS] = {
	{ .mni_type = GENOA_NBIF_T_DUMMY, .mni_dev = 0, .mni_func = 0 },
	{ .mni_type = GENOA_NBIF_T_NTB, .mni_dev = 0, .mni_func = 1 },
	{ .mni_type = GENOA_NBIF_T_NVME, .mni_dev = 0, .mni_func = 2 }
};

/*
 * This structure and the following table encodes the mapping of the set of dxio
 * lanes to a given PCIe core on an IOMS. This is ordered such that all of the
 * normal engines are present; however, the wafl core, being special is not
 * here. The dxio engine uses different lane numbers than the phys. Note, that
 * all lanes here are inclusive. e.g. [start, end].
 */
typedef struct genoa_pcie_core_info {
	const char	*mpci_name;
	uint16_t	mpci_dxio_start;
	uint16_t	mpci_dxio_end;
	uint16_t	mpci_phy_start;
	uint16_t	mpci_phy_end;
} genoa_pcie_core_info_t;

static const genoa_pcie_core_info_t genoa_lane_maps[8] = {
	{ "G0", 0x10, 0x1f, 0x10, 0x1f },
	{ "P0", 0x2a, 0x39, 0x00, 0x0f },
	{ "P1", 0x3a, 0x49, 0x20, 0x2f },
	{ "G1", 0x00, 0x0f, 0x30, 0x3f },
	{ "G3", 0x72, 0x81, 0x60, 0x6f },
	{ "P3", 0x5a, 0x69, 0x70, 0x7f },
	{ "P2", 0x4a, 0x59, 0x50, 0x5f },
	{ "G2", 0x82, 0x91, 0x40, 0x4f }
};

static const genoa_pcie_core_info_t genoa_wafl_map = {
	"WAFL", 0x24, 0x25, 0x80, 0x81
};

/*
 * How many PCIe cores does this NBIO instance have?
 */
uint8_t
genoa_nbio_n_pcie_cores(const uint8_t nbno)
{
	if (nbno == GENOA_IOMS_HAS_WAFL)
		return (GENOA_IOMS_MAX_PCIE_CORES);
	return (GENOA_IOMS_MAX_PCIE_CORES - 1);
}

/*
 * How many PCIe ports does this core instance have?  Not all ports are
 * necessarily enabled, and ports that are disabled may have their associated
 * bridges hidden; this is used to compute the locations of register blocks that
 * pertain to the port that may exist.
 */
uint8_t
genoa_pcie_core_n_ports(const uint8_t pcno)
{
	if (pcno == GENOA_IOMS_WAFL_PCIE_CORENO)
		return (GENOA_PCIE_CORE_WAFL_NPORTS);
	return (GENOA_PCIE_CORE_MAX_PORTS);
}

typedef enum genoa_iommul1_subunit {
	MIL1SU_NBIF,
	MIL1SU_IOAGR
} genoa_iommul1_subunit_t;

/*
 * XXX Belongs in a header.
 */
extern void *contig_alloc(size_t, ddi_dma_attr_t *, uintptr_t, int);
extern void contig_free(void *, size_t);

static bool genoa_smu_rpc_read_brand_string(genoa_iodie_t *,
    char *, size_t);

/*
 * Our primary global data. This is the reason that we exist.
 */
static genoa_fabric_t genoa_fabric;
static uint_t nthreads;

/*
 * Variable to let us dump all SMN traffic while still developing.
 */
int genoa_smn_log = 0;

static int
genoa_fabric_walk_iodie(genoa_fabric_t *fabric, genoa_iodie_cb_f func,
    void *arg)
{
	for (uint_t socno = 0; socno < fabric->gf_nsocs; socno++) {
		genoa_soc_t *soc = &fabric->gf_socs[socno];
		for (uint_t iono = 0; iono < soc->gs_ndies; iono++) {
			int ret;
			genoa_iodie_t *iodie = &soc->gs_iodies[iono];

			ret = func(iodie, arg);
			if (ret != 0) {
				return (ret);
			}
		}
	}

	return (0);
}

int
genoa_walk_iodie(genoa_iodie_cb_f func, void *arg)
{
	return (genoa_fabric_walk_iodie(&genoa_fabric, func, arg));
}

typedef struct genoa_fabric_ioms_cb {
	genoa_ioms_cb_f	mfic_func;
	void		*mfic_arg;
} genoa_fabric_ioms_cb_t;

static int
genoa_fabric_walk_ioms_iodie_cb(genoa_iodie_t *iodie, void *arg)
{
	genoa_fabric_ioms_cb_t *cb = arg;

	for (uint_t iomsno = 0; iomsno < iodie->gi_nioms; iomsno++) {
		int ret;
		genoa_ioms_t *ioms = &iodie->gi_ioms[iomsno];

		ret = cb->mfic_func(ioms, cb->mfic_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
genoa_fabric_walk_ioms(genoa_fabric_t *fabric, genoa_ioms_cb_f func, void *arg)
{
	genoa_fabric_ioms_cb_t cb;

	cb.mfic_func = func;
	cb.mfic_arg = arg;
	return (genoa_fabric_walk_iodie(fabric, genoa_fabric_walk_ioms_iodie_cb,
	    &cb));
}

int
genoa_walk_ioms(genoa_ioms_cb_f func, void *arg)
{
	return (genoa_fabric_walk_ioms(&genoa_fabric, func, arg));
}

typedef struct genoa_fabric_nbif_cb {
	genoa_nbif_cb_f	mfnc_func;
	void		*mfnc_arg;
} genoa_fabric_nbif_cb_t;

static int
genoa_fabric_walk_nbif_ioms_cb(genoa_ioms_t *ioms, void *arg)
{
	genoa_fabric_nbif_cb_t *cb = arg;

	for (uint_t nbifno = 0; nbifno < ioms->gio_nnbifs; nbifno++) {
		int ret;
		genoa_nbif_t *nbif = &ioms->gio_nbifs[nbifno];
		ret = cb->mfnc_func(nbif, cb->mfnc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
genoa_fabric_walk_nbif(genoa_fabric_t *fabric, genoa_nbif_cb_f func, void *arg)
{
	genoa_fabric_nbif_cb_t cb;

	cb.mfnc_func = func;
	cb.mfnc_arg = arg;
	return (genoa_fabric_walk_ioms(fabric, genoa_fabric_walk_nbif_ioms_cb,
	    &cb));
}

typedef struct genoa_fabric_pcie_core_cb {
	genoa_pcie_core_cb_f	mfpcc_func;
	void			*mfpcc_arg;
} genoa_fabric_pcie_core_cb_t;

static int
genoa_fabric_walk_pcie_core_cb(genoa_ioms_t *ioms, void *arg)
{
	genoa_fabric_pcie_core_cb_t *cb = arg;

	for (uint_t pcno = 0; pcno < ioms->gio_npcie_cores; pcno++) {
		int ret;
		genoa_pcie_core_t *pc = &ioms->gio_pcie_cores[pcno];

		ret = cb->mfpcc_func(pc, cb->mfpcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
genoa_fabric_walk_pcie_core(genoa_fabric_t *fabric, genoa_pcie_core_cb_f func,
    void *arg)
{
	genoa_fabric_pcie_core_cb_t cb;

	cb.mfpcc_func = func;
	cb.mfpcc_arg = arg;
	return (genoa_fabric_walk_ioms(fabric, genoa_fabric_walk_pcie_core_cb,
	    &cb));
}

typedef struct genoa_fabric_pcie_port_cb {
	genoa_pcie_port_cb_f	mfppc_func;
	void			*mfppc_arg;
} genoa_fabric_pcie_port_cb_t;

static int
genoa_fabric_walk_pcie_port_cb(genoa_pcie_core_t *pc, void *arg)
{
	genoa_fabric_pcie_port_cb_t *cb = arg;

	for (uint_t portno = 0; portno < pc->gpc_nports; portno++) {
		int ret;
		genoa_pcie_port_t *port = &pc->gpc_ports[portno];

		ret = cb->mfppc_func(port, cb->mfppc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
genoa_fabric_walk_pcie_port(genoa_fabric_t *fabric, genoa_pcie_port_cb_f func,
    void *arg)
{
	genoa_fabric_pcie_port_cb_t cb;

	cb.mfppc_func = func;
	cb.mfppc_arg = arg;
	return (genoa_fabric_walk_pcie_core(fabric,
	    genoa_fabric_walk_pcie_port_cb, &cb));
}

typedef struct genoa_fabric_ccd_cb {
	genoa_ccd_cb_f	mfcc_func;
	void		*mfcc_arg;
} genoa_fabric_ccd_cb_t;

static int
genoa_fabric_walk_ccd_iodie_cb(genoa_iodie_t *iodie, void *arg)
{
	genoa_fabric_ccd_cb_t *cb = arg;

	for (uint8_t ccdno = 0; ccdno < iodie->gi_nccds; ccdno++) {
		int ret;
		genoa_ccd_t *ccd = &iodie->gi_ccds[ccdno];

		if ((ret = cb->mfcc_func(ccd, cb->mfcc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
genoa_fabric_walk_ccd(genoa_fabric_t *fabric, genoa_ccd_cb_f func, void *arg)
{
	genoa_fabric_ccd_cb_t cb;

	cb.mfcc_func = func;
	cb.mfcc_arg = arg;
	return (genoa_fabric_walk_iodie(fabric,
	    genoa_fabric_walk_ccd_iodie_cb, &cb));
}

typedef struct genoa_fabric_ccx_cb {
	genoa_ccx_cb_f	mfcc_func;
	void		*mfcc_arg;
} genoa_fabric_ccx_cb_t;

static int
genoa_fabric_walk_ccx_ccd_cb(genoa_ccd_t *ccd, void *arg)
{
	genoa_fabric_ccx_cb_t *cb = arg;

	for (uint8_t ccxno = 0; ccxno < ccd->gcd_nccxs; ccxno++) {
		int ret;
		genoa_ccx_t *ccx = &ccd->gcd_ccxs[ccxno];

		if ((ret = cb->mfcc_func(ccx, cb->mfcc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
genoa_fabric_walk_ccx(genoa_fabric_t *fabric, genoa_ccx_cb_f func, void *arg)
{
	genoa_fabric_ccx_cb_t cb;

	cb.mfcc_func = func;
	cb.mfcc_arg = arg;
	return (genoa_fabric_walk_ccd(fabric,
	    genoa_fabric_walk_ccx_ccd_cb, &cb));
}

typedef struct genoa_fabric_core_cb {
	genoa_core_cb_f	mfcc_func;
	void		*mfcc_arg;
} genoa_fabric_core_cb_t;

static int
genoa_fabric_walk_core_ccx_cb(genoa_ccx_t *ccx, void *arg)
{
	genoa_fabric_core_cb_t *cb = arg;

	for (uint8_t coreno = 0; coreno < ccx->gcx_ncores; coreno++) {
		int ret;
		genoa_core_t *core = &ccx->gcx_cores[coreno];

		if ((ret = cb->mfcc_func(core, cb->mfcc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
genoa_fabric_walk_core(genoa_fabric_t *fabric, genoa_core_cb_f func, void *arg)
{
	genoa_fabric_core_cb_t cb;

	cb.mfcc_func = func;
	cb.mfcc_arg = arg;
	return (genoa_fabric_walk_ccx(fabric,
	    genoa_fabric_walk_core_ccx_cb, &cb));
}

typedef struct genoa_fabric_thread_cb {
	genoa_thread_cb_f	mftc_func;
	void			*mftc_arg;
} genoa_fabric_thread_cb_t;

static int
genoa_fabric_walk_thread_core_cb(genoa_core_t *core, void *arg)
{
	genoa_fabric_thread_cb_t *cb = arg;

	for (uint8_t threadno = 0; threadno < core->gc_nthreads; threadno++) {
		int ret;
		genoa_thread_t *thread = &core->gc_threads[threadno];

		if ((ret = cb->mftc_func(thread, cb->mftc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
genoa_fabric_walk_thread(genoa_fabric_t *fabric,
    genoa_thread_cb_f func, void *arg)
{
	genoa_fabric_thread_cb_t cb;

	cb.mftc_func = func;
	cb.mftc_arg = arg;
	return (genoa_fabric_walk_core(fabric,
	    genoa_fabric_walk_thread_core_cb, &cb));
}

int
genoa_walk_thread(genoa_thread_cb_f func, void *arg)
{
	return (genoa_fabric_walk_thread(&genoa_fabric, func, arg));
}

typedef struct {
	uint32_t	mffi_dest;
	genoa_ioms_t	*mffi_ioms;
} genoa_fabric_find_ioms_t;

static int
genoa_fabric_find_ioms_cb(genoa_ioms_t *ioms, void *arg)
{
	genoa_fabric_find_ioms_t *mffi = arg;

	/*
	 * Note the DstFabricId is for the IOS and not the IOM.
	 */
	if (mffi->mffi_dest == ioms->gio_ios_fabric_id) {
		mffi->mffi_ioms = ioms;
	}

	return (0);
}

static int
genoa_fabric_find_ioms_by_bus_cb(genoa_ioms_t *ioms, void *arg)
{
	genoa_fabric_find_ioms_t *mffi = arg;

	if (mffi->mffi_dest == ioms->gio_pci_busno) {
		mffi->mffi_ioms = ioms;
	}

	return (0);
}

static genoa_ioms_t *
genoa_fabric_find_ioms(genoa_fabric_t *fabric, uint32_t destid)
{
	genoa_fabric_find_ioms_t mffi;

	mffi.mffi_dest = destid;
	mffi.mffi_ioms = NULL;

	genoa_fabric_walk_ioms(fabric, genoa_fabric_find_ioms_cb, &mffi);

	return (mffi.mffi_ioms);
}

static genoa_ioms_t *
genoa_fabric_find_ioms_by_bus(genoa_fabric_t *fabric, uint32_t pci_bus)
{
	genoa_fabric_find_ioms_t mffi;

	mffi.mffi_dest = pci_bus;
	mffi.mffi_ioms = NULL;

	genoa_fabric_walk_ioms(fabric, genoa_fabric_find_ioms_by_bus_cb, &mffi);

	return (mffi.mffi_ioms);
}

typedef struct {
	const genoa_iodie_t *mffpc_iodie;
	uint16_t mffpc_start;
	uint16_t mffpc_end;
	genoa_pcie_core_t *mffpc_pc;
} genoa_fabric_find_pcie_core_t;

static int
genoa_fabric_find_pcie_core_by_lanes_cb(genoa_pcie_core_t *pc, void *arg)
{
	genoa_fabric_find_pcie_core_t *mffpc = arg;

	if (mffpc->mffpc_iodie != pc->gpc_ioms->gio_iodie) {
		return (0);
	}

	if (mffpc->mffpc_start >= pc->gpc_dxio_lane_start &&
	    mffpc->mffpc_start <= pc->gpc_dxio_lane_end &&
	    mffpc->mffpc_end >= pc->gpc_dxio_lane_start &&
	    mffpc->mffpc_end <= pc->gpc_dxio_lane_end) {
		mffpc->mffpc_pc = pc;
		return (1);
	}

	return (0);
}


static genoa_pcie_core_t *
genoa_fabric_find_pcie_core_by_lanes(genoa_iodie_t *iodie,
    uint16_t start, uint16_t end)
{
	genoa_fabric_find_pcie_core_t mffpc;

	mffpc.mffpc_iodie = iodie;
	mffpc.mffpc_start = start;
	mffpc.mffpc_end = end;
	mffpc.mffpc_pc = NULL;
	ASSERT3U(start, <=, end);

	(void) genoa_fabric_walk_pcie_core(iodie->gi_soc->gs_fabric,
	    genoa_fabric_find_pcie_core_by_lanes_cb, &mffpc);

	return (mffpc.mffpc_pc);
}

typedef struct genoa_fabric_find_thread {
	uint32_t	mfft_search;
	uint32_t	mfft_count;
	genoa_thread_t	*mfft_found;
} genoa_fabric_find_thread_t;

static int
genoa_fabric_find_thread_by_cpuid_cb(genoa_thread_t *thread, void *arg)
{
	genoa_fabric_find_thread_t *mfft = arg;
	if (mfft->mfft_count == mfft->mfft_search) {
		mfft->mfft_found = thread;
		return (1);
	}
	++mfft->mfft_count;
	return (0);
}

genoa_thread_t *
genoa_fabric_find_thread_by_cpuid(uint32_t cpuid)
{
	genoa_fabric_find_thread_t mfft;

	mfft.mfft_search = cpuid;
	mfft.mfft_count = 0;
	mfft.mfft_found = NULL;
	(void) genoa_fabric_walk_thread(&genoa_fabric,
	    genoa_fabric_find_thread_by_cpuid_cb, &mfft);

	return (mfft.mfft_found);
}

/*
 * buf, len, and return value semantics match those of snprintf(9f).
 */
size_t
genoa_fabric_thread_get_brandstr(const genoa_thread_t *thread,
    char *buf, size_t len)
{
	genoa_soc_t *soc = thread->gt_core->gc_ccx->gcx_ccd->gcd_iodie->gi_soc;
	return (snprintf(buf, len, "%s", soc->gs_brandstr));
}

void
genoa_fabric_thread_get_dpm_weights(const genoa_thread_t *thread,
    const uint64_t **wp, uint32_t *nentp)
{
	genoa_iodie_t *iodie = thread->gt_core->gc_ccx->gcx_ccd->gcd_iodie;
	*wp = iodie->gi_dpm_weights;
	*nentp = GENOA_MAX_DPM_WEIGHTS;
}

uint64_t
genoa_fabric_ecam_base(void)
{
	uint64_t ecam = genoa_fabric.gf_ecam_base;

	ASSERT3U(ecam, !=, 0);

	return (ecam);
}

static uint32_t
genoa_df_read32(genoa_iodie_t *iodie, uint8_t inst, const df_reg_def_t def)
{
	uint32_t val = 0;
	const df_reg_def_t ficaa = DF_FICAA_V4;
	const df_reg_def_t ficad = DF_FICAD_LO_V4;

	mutex_enter(&iodie->gi_df_ficaa_lock);
	ASSERT3U(def.drd_gens & DF_REV_3, ==, DF_REV_3);
	val = DF_FICAA_V2_SET_TARG_INST(val, 1);
	val = DF_FICAA_V2_SET_FUNC(val, def.drd_func);
	val = DF_FICAA_V2_SET_INST(val, inst);
	val = DF_FICAA_V2_SET_64B(val, 0);
	val = DF_FICAA_V4_SET_REG(val, def.drd_reg >> 2);

	ASSERT0(ficaa.drd_reg & 3);
	pci_putl_func(0, iodie->gi_dfno, ficaa.drd_func, ficaa.drd_reg, val);
	val = pci_getl_func(0, iodie->gi_dfno, ficad.drd_func, ficad.drd_reg);
	mutex_exit(&iodie->gi_df_ficaa_lock);

	return (val);
}

/*
 * A broadcast read is allowed to use PCIe configuration space directly to read
 * the register. Because we are not using the indirect registers, there is no
 * locking being used as the purpose of gi_df_ficaa_lock is just to ensure
 * there's only one use of it at any given time.
 */
static uint32_t
genoa_df_bcast_read32(genoa_iodie_t *iodie, const df_reg_def_t def)
{
	ASSERT0(def.drd_reg & 3);
	return (pci_getl_func(0, iodie->gi_dfno, def.drd_func, def.drd_reg));
}

static void
genoa_df_bcast_write32(genoa_iodie_t *iodie, const df_reg_def_t def,
    uint32_t val)
{
	ASSERT0(def.drd_reg & 3);
	pci_putl_func(0, iodie->gi_dfno, def.drd_func, def.drd_reg, val);
}

/*
 * This is used early in boot when we're trying to bootstrap the system so we
 * can construct our fabric data structure. This always reads against the first
 * data fabric instance which is required to be present.
 */
static uint32_t
genoa_df_early_read32(const df_reg_def_t def)
{
	ASSERT0(def.drd_reg & 3);
	return (pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE,
	    def.drd_func, def.drd_reg));
}

uint32_t
genoa_smn_read(genoa_iodie_t *iodie, const smn_reg_t reg)
{
	const uint32_t addr = SMN_REG_ADDR(reg);
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);
	uint32_t val;

	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));

	mutex_enter(&iodie->gi_smn_lock);
	pci_putl_func(iodie->gi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, base_addr);
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		val = (uint32_t)pci_getb_func(iodie->gi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA + addr_off);
		break;
	case 2:
		val = (uint32_t)pci_getw_func(iodie->gi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA + addr_off);
		break;
	case 4:
		val = pci_getl_func(iodie->gi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA);
		break;
	default:
		panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}
	if (genoa_smn_log != 0) {
		cmn_err(CE_NOTE, "SMN R reg 0x%x: 0x%x", addr, val);
	}
	mutex_exit(&iodie->gi_smn_lock);

	return (val);
}

void
genoa_smn_write(genoa_iodie_t *iodie, const smn_reg_t reg, const uint32_t val)
{
	const uint32_t addr = SMN_REG_ADDR(reg);
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);

	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));
	ASSERT(SMN_REG_VALUE_FITS(reg, val));

	mutex_enter(&iodie->gi_smn_lock);
	if (genoa_smn_log != 0) {
		cmn_err(CE_NOTE, "SMN W reg 0x%x: 0x%x", addr, val);
	}
	pci_putl_func(iodie->gi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, base_addr);
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		pci_putb_func(iodie->gi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA + addr_off,
		    (uint8_t)val);
		break;
	case 2:
		pci_putw_func(iodie->gi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA + addr_off,
		    (uint16_t)val);
		break;
	case 4:
		pci_putl_func(iodie->gi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA, val);
		break;
	default:
		panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}

	mutex_exit(&iodie->gi_smn_lock);
}

/*
 * Convenience functions for accessing SMN registers pertaining to a bridge.
 * These are candidates for making public if/when other code needs to manipulate
 * bridges.  There are some tradeoffs here: we don't need any of these
 * functions; callers could instead look up registers themselves, retrieve the
 * iodie by chasing back-pointers, and call genoa_smn_{read,write}32()
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

static smn_reg_t
genoa_pcie_port_reg(const genoa_pcie_port_t *const port,
    const smn_reg_def_t def)
{
	genoa_pcie_core_t *pc = port->gpp_core;
	genoa_ioms_t *ioms = pc->gpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_IOHCDEV_PCIE:
		reg = genoa_iohcdev_pcie_smn_reg(ioms->gio_num, def,
		    pc->gpc_coreno, port->gpp_portno);
		break;
	case SMN_UNIT_PCIE_PORT:
		reg = genoa_pcie_port_smn_reg(ioms->gio_num, def,
		    pc->gpc_coreno, port->gpp_portno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe port",
		    def.srd_unit);
	}

	return (reg);
}

static uint32_t
genoa_pcie_port_read(genoa_pcie_port_t *port, const smn_reg_t reg)
{
	genoa_iodie_t *iodie = port->gpp_core->gpc_ioms->gio_iodie;

	return (genoa_smn_read(iodie, reg));
}

static void
genoa_pcie_port_write(genoa_pcie_port_t *port, const smn_reg_t reg,
    const uint32_t val)
{
	genoa_iodie_t *iodie = port->gpp_core->gpc_ioms->gio_iodie;

	genoa_smn_write(iodie, reg, val);
}

static smn_reg_t
genoa_pcie_core_reg(const genoa_pcie_core_t *const pc, const smn_reg_def_t def)
{
	genoa_ioms_t *ioms = pc->gpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_CORE:
		reg = genoa_pcie_core_smn_reg(ioms->gio_num, def,
		    pc->gpc_coreno);
		break;
	case SMN_UNIT_PCIE_RSMU:
		reg = genoa_pcie_rsmu_smn_reg(ioms->gio_num, def,
		    pc->gpc_coreno);
		break;
	case SMN_UNIT_IOMMUL1:
		reg = genoa_iommul1_pcie_smn_reg(ioms->gio_num, def,
		    pc->gpc_coreno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe RC",
		    def.srd_unit);
	}

	return (reg);
}

static uint32_t
genoa_pcie_core_read(genoa_pcie_core_t *pc, const smn_reg_t reg)
{
	genoa_iodie_t *iodie = pc->gpc_ioms->gio_iodie;

	return (genoa_smn_read(iodie, reg));
}

static void
genoa_pcie_core_write(genoa_pcie_core_t *pc, const smn_reg_t reg,
    const uint32_t val)
{
	genoa_iodie_t *iodie = pc->gpc_ioms->gio_iodie;

	genoa_smn_write(iodie, reg, val);
}

/*
 * We consider the IOAGR to be part of the NBIO/IOHC/IOMS, so the IOMMUL1's
 * IOAGR block falls under the IOMS; the IOAPIC, SDPMUX, and IOMMUL2 are similar
 * as they do not (currently) have independent representation in the fabric.
 */

smn_reg_t
genoa_ioms_reg(const genoa_ioms_t *const ioms, const smn_reg_def_t def,
    const uint16_t reginst)
{
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_IOAPIC:
		reg = genoa_ioapic_smn_reg(ioms->gio_num, def, reginst);
		break;
	case SMN_UNIT_IOHC:
		reg = genoa_iohc_smn_reg(ioms->gio_num, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = genoa_ioagr_smn_reg(ioms->gio_num, def, reginst);
		break;
	case SMN_UNIT_SDPMUX:
		reg = genoa_sdpmux_smn_reg(ioms->gio_num, def, reginst);
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
		const genoa_iommul1_subunit_t su =
		    (const genoa_iommul1_subunit_t)reginst;
		switch (su) {
		case MIL1SU_NBIF:
			reg = genoa_iommul1_nbif_smn_reg(ioms->gio_num, def, 0);
			break;
		case MIL1SU_IOAGR:
			reg = genoa_iommul1_ioagr_smn_reg(ioms->gio_num,
			    def, 0);
			break;
		default:
			cmn_err(CE_PANIC, "invalid IOMMUL1 subunit %d", su);
			break;
		}
		break;
	}
	case SMN_UNIT_IOMMUL2:
		reg = genoa_iommul2_smn_reg(ioms->gio_num, def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
		    def.srd_unit);
	}

	return (reg);
}

uint32_t
genoa_ioms_read(genoa_ioms_t *ioms, const smn_reg_t reg)
{
	genoa_iodie_t *iodie = ioms->gio_iodie;

	return (genoa_smn_read(iodie, reg));
}

void
genoa_ioms_write(genoa_ioms_t *ioms, const smn_reg_t reg, const uint32_t val)
{
	genoa_iodie_t *iodie = ioms->gio_iodie;

	genoa_smn_write(iodie, reg, val);
}

static smn_reg_t
genoa_nbif_reg(const genoa_nbif_t *const nbif, const smn_reg_def_t def,
    const uint16_t reginst)
{
	genoa_ioms_t *ioms = nbif->gn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF:
		reg = genoa_nbif_smn_reg(ioms->gio_num, def, nbif->gn_nbifno,
		    reginst);
		break;
	case SMN_UNIT_NBIF_ALT:
		reg = genoa_nbif_alt_smn_reg(ioms->gio_num, def,
		    nbif->gn_nbifno, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF",
		    def.srd_unit);
	}

	return (reg);
}

static uint32_t
genoa_nbif_read(genoa_nbif_t *nbif, const smn_reg_t reg)
{
	return (genoa_smn_read(nbif->gn_ioms->gio_iodie, reg));
}

static void
genoa_nbif_write(genoa_nbif_t *nbif, const smn_reg_t reg, const uint32_t val)
{
	genoa_smn_write(nbif->gn_ioms->gio_iodie, reg, val);
}

static smn_reg_t
genoa_nbif_func_reg(const genoa_nbif_func_t *const func,
    const smn_reg_def_t def)
{
	genoa_nbif_t *nbif = func->gne_nbif;
	genoa_ioms_t *ioms = nbif->gn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF_FUNC:
		reg = genoa_nbif_func_smn_reg(ioms->gio_num, def,
		    nbif->gn_nbifno, func->gne_dev, func->gne_func);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF func",
		    def.srd_unit);
	}

	return (reg);
}

static uint32_t
genoa_nbif_func_read(genoa_nbif_func_t *func, const smn_reg_t reg)
{
	return (genoa_smn_read(func->gne_nbif->gn_ioms->gio_iodie, reg));
}

static void
genoa_nbif_func_write(genoa_nbif_func_t *func, const smn_reg_t reg,
    const uint32_t val)
{
	genoa_smn_write(func->gne_nbif->gn_ioms->gio_iodie, reg, val);
}

smn_reg_t
genoa_iodie_reg(const genoa_iodie_t *const iodie, const smn_reg_def_t def,
    const uint16_t reginst)
{
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_SMU_RPC:
		reg = genoa_smu_smn_reg(0, def, reginst);
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

uint32_t
genoa_iodie_read(genoa_iodie_t *iodie, const smn_reg_t reg)
{
	return (genoa_smn_read(iodie, reg));
}

void
genoa_iodie_write(genoa_iodie_t *iodie, const smn_reg_t reg, const uint32_t val)
{
	genoa_smn_write(iodie, reg, val);
}

uint8_t
genoa_iodie_node_id(const genoa_iodie_t *const iodie)
{
	return (iodie->gi_node_id);
}

genoa_iodie_flag_t
genoa_iodie_flags(const genoa_iodie_t *const iodie)
{
	return (iodie->gi_flags);
}

genoa_ioms_flag_t
genoa_ioms_flags(const genoa_ioms_t *const ioms)
{
	return (ioms->gio_flags);
}

genoa_iodie_t *
genoa_ioms_iodie(const genoa_ioms_t *const ioms)
{
	return (ioms->gio_iodie);
}

typedef enum {
	MBT_ANY,
	MBT_RUBY,
	MBT_COSMO,
} genoa_board_type_t;

/*
 * Here is a temporary rough heuristic for determining what board we're on.
 */
static genoa_board_type_t
genoa_board_type(const genoa_fabric_t *fabric)
{
	if (fabric->gf_nsocs == 1) {
		return (MBT_RUBY);
	}
	return (MBT_ANY);
}

/*
 * We pass these functions 64 bits of debug data consisting of 32 bits of stage
 * number and 8 bits containing the I/O die index for which to capture register
 * values.  A value of 0xff, which is never valid for any I/O die, means capture
 * all of them.  These inline functions encode and decode this argument; we use
 * functions so they are typed.
 */
#define	GENOA_IODIE_MATCH_ANY	0xff

static inline void *
genoa_pcie_dbg_cookie(genoa_pcie_config_stage_t stage, uint8_t iodie)
{
	uintptr_t rv;

	rv = (uintptr_t)stage;
	rv |= ((uintptr_t)iodie) << 32;

	return ((void *)rv);
}

static inline genoa_pcie_config_stage_t
genoa_pcie_dbg_cookie_to_stage(void *arg)
{
	uintptr_t av = (uintptr_t)arg;

	return ((genoa_pcie_config_stage_t)(av & UINT32_MAX));
}

static inline uint8_t
genoa_pcie_dbg_cookie_to_iodie(void *arg)
{
	uintptr_t av = (uintptr_t)arg;

	return ((uint8_t)(av >> 32));
}

static int
genoa_pcie_populate_core_dbg(genoa_pcie_core_t *pc, void *arg)
{
	genoa_pcie_config_stage_t stage = genoa_pcie_dbg_cookie_to_stage(arg);
	uint8_t iodie_match = genoa_pcie_dbg_cookie_to_iodie(arg);
	genoa_pcie_dbg_t *dp = pc->gpc_dbg;

	if (dp == NULL)
		return (0);

	if (iodie_match != GENOA_IODIE_MATCH_ANY &&
	    iodie_match != pc->gpc_ioms->gio_iodie->gi_node_id) {
		return (0);
	}

	for (uint_t rn = 0; rn < dp->gpd_nregs; rn++) {
		smn_reg_t reg;

		reg = genoa_pcie_core_reg(pc, dp->gpd_regs[rn].gprd_def);
		dp->gpd_regs[rn].gprd_val[stage] =
		    genoa_pcie_core_read(pc, reg);
		dp->gpd_regs[rn].gprd_ts[stage] = gethrtime();
	}

	dp->gpd_last_stage = stage;

	return (0);
}

static int
genoa_pcie_populate_port_dbg(genoa_pcie_port_t *port, void *arg)
{
	genoa_pcie_config_stage_t stage = genoa_pcie_dbg_cookie_to_stage(arg);
	uint8_t iodie_match = genoa_pcie_dbg_cookie_to_iodie(arg);
	genoa_pcie_dbg_t *dp = port->gpp_dbg;

	if (dp == NULL)
		return (0);

	if (iodie_match != GENOA_IODIE_MATCH_ANY &&
	    iodie_match != port->gpp_core->gpc_ioms->gio_iodie->gi_node_id) {
		return (0);
	}

	for (uint_t rn = 0; rn < dp->gpd_nregs; rn++) {
		smn_reg_t reg;

		reg = genoa_pcie_port_reg(port, dp->gpd_regs[rn].gprd_def);
		dp->gpd_regs[rn].gprd_val[stage] =
		    genoa_pcie_port_read(port, reg);
		dp->gpd_regs[rn].gprd_ts[stage] = gethrtime();
	}

	dp->gpd_last_stage = stage;

	return (0);
}

static void
genoa_pcie_populate_dbg(genoa_fabric_t *fabric, genoa_pcie_config_stage_t stage,
    uint8_t iodie_match)
{
	static bool gpio_configured;
	void *cookie = genoa_pcie_dbg_cookie(stage, iodie_match);

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
	if (genoa_board_type(fabric) == MBT_COSMO) {
		if (!gpio_configured) {
			genoa_hack_gpio(GHGOP_CONFIGURE, 129);
			genoa_hack_gpio(GHGOP_TOGGLE, 129);
			drv_usecwait(1);
			gpio_configured = true;
		}
		genoa_hack_gpio(GHGOP_TOGGLE, 129);
	}

	(void) genoa_fabric_walk_pcie_core(fabric, genoa_pcie_populate_core_dbg,
	    cookie);
	(void) genoa_fabric_walk_pcie_port(fabric, genoa_pcie_populate_port_dbg,
	    cookie);
}

static void
genoa_fabric_ioms_pcie_init(genoa_ioms_t *ioms)
{
	for (uint_t pcno = 0; pcno < ioms->gio_npcie_cores; pcno++) {
		genoa_pcie_core_t *pc = &ioms->gio_pcie_cores[pcno];
		const genoa_pcie_port_info_t *pinfop = NULL;
		const genoa_pcie_core_info_t *cinfop;

		pc->gpc_coreno = pcno;
		pc->gpc_ioms = ioms;
		pc->gpc_nports = genoa_pcie_core_n_ports(pcno);
		mutex_init(&pc->gpc_strap_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));

		VERIFY3U(pcno, <=, GENOA_IOMS_WAFL_PCIE_CORENO);
		switch (pcno) {
		case 0:
			/* XXX Macros */
			pc->gpc_sdp_unit = 2;
			pc->gpc_sdp_port = 0;
			pinfop = genoa_pcie0;
			break;
		case 1:
			pc->gpc_sdp_unit = 3;
			pc->gpc_sdp_port = 0;
			pinfop = genoa_pcie1;
			break;
		case GENOA_IOMS_WAFL_PCIE_CORENO:
			pc->gpc_sdp_unit = 4;
			pc->gpc_sdp_port = 5;
			pinfop = genoa_pcie2;
			break;
		}

		if (pcno == GENOA_IOMS_WAFL_PCIE_CORENO) {
			cinfop = &genoa_wafl_map;
		} else {
			cinfop = &genoa_lane_maps[ioms->gio_num * 2 + pcno];
		}

		pc->gpc_dxio_lane_start = cinfop->mpci_dxio_start;
		pc->gpc_dxio_lane_end = cinfop->mpci_dxio_end;
		pc->gpc_phys_lane_start = cinfop->mpci_phy_start;
		pc->gpc_phys_lane_end = cinfop->mpci_phy_end;

		for (uint_t portno = 0; portno < pc->gpc_nports; portno++) {
			genoa_pcie_port_t *port = &pc->gpc_ports[portno];

			port->gpp_portno = portno;
			port->gpp_core = pc;
			port->gpp_device = pinfop[portno].mppi_dev;
			port->gpp_func = pinfop[portno].mppi_func;
			port->gpp_hp_type = SMU_HP_INVALID;
		}
	}
}

static void
genoa_fabric_ioms_nbif_init(genoa_ioms_t *ioms)
{
	for (uint_t nbifno = 0; nbifno < ioms->gio_nnbifs; nbifno++) {
		const genoa_nbif_info_t *ninfo = NULL;
		genoa_nbif_t *nbif = &ioms->gio_nbifs[nbifno];

		nbif->gn_nbifno = nbifno;
		nbif->gn_ioms = ioms;
		VERIFY3U(nbifno, <, GENOA_IOMS_MAX_NBIF);
		switch (nbifno) {
		case 0:
			nbif->gn_nfuncs = GENOA_NBIF0_NFUNCS;
			ninfo = genoa_nbif0;
			break;
		case 1:
			nbif->gn_nfuncs = GENOA_NBIF1_NFUNCS;
			ninfo = genoa_nbif1;
			break;
		case 2:
			nbif->gn_nfuncs = GENOA_NBIF2_NFUNCS;
			ninfo = genoa_nbif2;
			break;
		}

		for (uint_t funcno = 0; funcno < nbif->gn_nfuncs; funcno++) {
			genoa_nbif_func_t *func = &nbif->gn_funcs[funcno];

			func->gne_nbif = nbif;
			func->gne_type = ninfo[funcno].mni_type;
			func->gne_dev = ninfo[funcno].mni_dev;
			func->gne_func = ninfo[funcno].mni_func;

			/*
			 * As there is a dummy device on each of these, this in
			 * theory doesn't need any explicit configuration.
			 */
			if (func->gne_type == GENOA_NBIF_T_DUMMY) {
				func->gne_flags |= GENOA_NBIF_F_NO_CONFIG;
			}
		}
	}
}

static bool
genoa_smu_version_at_least(const genoa_iodie_t *iodie,
    const uint8_t major, const uint8_t minor, const uint8_t patch)
{
	return (iodie->gi_smu_fw[0] > major ||
	    (iodie->gi_smu_fw[0] == major && iodie->gi_smu_fw[1] > minor) ||
	    (iodie->gi_smu_fw[0] == major && iodie->gi_smu_fw[1] == minor &&
	    iodie->gi_smu_fw[2] >= patch));
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
genoa_smu_dma_attr(ddi_dma_attr_t *attr)
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

static void
genoa_smu_rpc(genoa_iodie_t *iodie, genoa_smu_rpc_t *rpc)
{
	uint32_t resp;

	mutex_enter(&iodie->gi_smu_lock);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_RESP(), GENOA_SMU_RPC_NOTDONE);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_ARG0(), rpc->msr_arg0);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_ARG1(), rpc->msr_arg1);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_ARG2(), rpc->msr_arg2);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_ARG3(), rpc->msr_arg3);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_ARG4(), rpc->msr_arg4);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_ARG5(), rpc->msr_arg5);
	genoa_iodie_write(iodie, GENOA_SMU_RPC_REQ(), rpc->msr_req);

	/*
	 * XXX Infinite spins are bad, but we don't even have drv_usecwait yet.
	 * When we add a timeout this should then return an int.
	 */
	for (;;) {
		resp = genoa_iodie_read(iodie, GENOA_SMU_RPC_RESP());
		if (resp != GENOA_SMU_RPC_NOTDONE) {
			break;
		}
	}

	rpc->msr_resp = resp;
	if (rpc->msr_resp == GENOA_SMU_RPC_OK) {
		rpc->msr_arg0 = genoa_iodie_read(iodie, GENOA_SMU_RPC_ARG0());
		rpc->msr_arg1 = genoa_iodie_read(iodie, GENOA_SMU_RPC_ARG1());
		rpc->msr_arg2 = genoa_iodie_read(iodie, GENOA_SMU_RPC_ARG2());
		rpc->msr_arg3 = genoa_iodie_read(iodie, GENOA_SMU_RPC_ARG3());
		rpc->msr_arg4 = genoa_iodie_read(iodie, GENOA_SMU_RPC_ARG4());
		rpc->msr_arg5 = genoa_iodie_read(iodie, GENOA_SMU_RPC_ARG5());
	}
	mutex_exit(&iodie->gi_smu_lock);
}

static bool
genoa_smu_rpc_get_version(genoa_iodie_t *iodie, uint8_t *major, uint8_t *minor,
    uint8_t *patch)
{
	genoa_smu_rpc_t rpc = { 0 };

	rpc.msr_req = GENOA_SMU_OP_GET_VERSION;
	genoa_smu_rpc(iodie, &rpc);
	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		return (false);
	}

	*major = GENOA_SMU_OP_GET_VERSION_MAJOR(rpc.msr_arg0);
	*minor = GENOA_SMU_OP_GET_VERSION_MINOR(rpc.msr_arg0);
	*patch = GENOA_SMU_OP_GET_VERSION_PATCH(rpc.msr_arg0);

	return (true);
}

static bool
genoa_smu_rpc_i2c_switch(genoa_iodie_t *iodie, uint32_t addr)
{
	genoa_smu_rpc_t rpc = { 0 };

	rpc.msr_req = GENOA_SMU_OP_I2C_SWITCH_ADDR;
	rpc.msr_arg0 = addr;
	genoa_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Set i2c address RPC Failed: addr: 0x%x, "
		    "SMU 0x%x", addr, rpc.msr_resp);
	}

	return (rpc.msr_resp == GENOA_SMU_RPC_OK);
}

static bool
genoa_smu_rpc_give_address(genoa_iodie_t *iodie, genoa_smu_addr_kind_t kind,
    uint64_t addr)
{
	genoa_smu_rpc_t rpc = { 0 };

	switch (kind) {
	case MSAK_GENERIC:
		rpc.msr_req = GENOA_SMU_OP_HAVE_AN_ADDRESS;
		break;
	case MSAK_HOTPLUG:
		/*
		 * For a long time, hotplug table addresses were provided to the
		 * SMU in the same manner as any others; however, in recent
		 * versions there is a separate RPC for that.
		 */
		rpc.msr_req = genoa_smu_version_at_least(iodie, 45, 90, 0) ?
		    GENOA_SMU_OP_HAVE_A_HP_ADDRESS :
		    GENOA_SMU_OP_HAVE_AN_ADDRESS;
		break;
	default:
		panic("invalid SMU address kind %d", (int)kind);
	}
	rpc.msr_arg0 = bitx64(addr, 31, 0);
	rpc.msr_arg1 = bitx64(addr, 63, 32);
	genoa_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Have an Address RPC Failed: addr: 0x%lx, "
		    "SMU req 0x%x resp 0x%x", addr, rpc.msr_req, rpc.msr_resp);
	}

	return (rpc.msr_resp == GENOA_SMU_RPC_OK);

}

static bool
genoa_smu_rpc_send_hotplug_table(genoa_iodie_t *iodie)
{
	genoa_smu_rpc_t rpc = { 0 };

	rpc.msr_req = GENOA_SMU_OP_TX_PCIE_HP_TABLE;
	genoa_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU TX Hotplug Table Failed: SMU 0x%x",
		    rpc.msr_resp);
	}

	return (rpc.msr_resp == GENOA_SMU_RPC_OK);
}

static bool
genoa_smu_rpc_hotplug_flags(genoa_iodie_t *iodie, uint32_t flags)
{
	genoa_smu_rpc_t rpc = { 0 };

	rpc.msr_req = GENOA_SMU_OP_SET_HOPTLUG_FLAGS;
	rpc.msr_arg0 = flags;
	genoa_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Set Hotplug Flags failed: SMU 0x%x",
		    rpc.msr_resp);
	}

	return (rpc.msr_resp == GENOA_SMU_RPC_OK);
}
static bool
genoa_smu_rpc_start_hotplug(genoa_iodie_t *iodie, bool one_based, uint8_t flags)
{
	genoa_smu_rpc_t rpc = { 0 };

	rpc.msr_req = GENOA_SMU_OP_START_HOTPLUG;
	if (one_based) {
		rpc.msr_arg0 = 1;
	}
	rpc.msr_arg0 |= flags;
	genoa_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Start Yer Hotplug Failed: SMU 0x%x",
		    rpc.msr_resp);
	}

	return (rpc.msr_resp == GENOA_SMU_RPC_OK);
}

/*
 * buf and len semantics here match those of snprintf
 */
static bool
genoa_smu_rpc_read_brand_string(genoa_iodie_t *iodie, char *buf, size_t len)
{
	genoa_smu_rpc_t rpc = { 0 };
	uint_t off;

	len = MIN(len, CPUID_BRANDSTR_STRLEN + 1);
	buf[len - 1] = '\0';
	rpc.msr_req = GENOA_SMU_OP_GET_BRAND_STRING;

	for (off = 0; off * 4 < len - 1; off++) {
		rpc.msr_arg0 = off;
		genoa_smu_rpc(iodie, &rpc);

		if (rpc.msr_resp != GENOA_SMU_RPC_OK)
			return (false);

		bcopy(&rpc.msr_arg0, buf + off * 4, len - off * 4);
	}

	return (true);
}

static bool
genoa_smu_rpc_read_dpm_weights(genoa_iodie_t *iodie, uint64_t *buf, size_t len)
{
	genoa_smu_rpc_t rpc = { 0 };

	len = MIN(len, GENOA_MAX_DPM_WEIGHTS * sizeof (uint64_t));
	bzero(buf, len);
	rpc.msr_req = GENOA_SMU_OP_READ_DPM_WEIGHT;

	for (uint32_t idx = 0; idx < len / sizeof (uint64_t); idx++) {
		rpc.msr_arg0 = idx;
		genoa_smu_rpc(iodie, &rpc);

		if (rpc.msr_resp != GENOA_SMU_RPC_OK)
			return (false);

		buf[idx] = rpc.msr_arg1;
		buf[idx] <<= 32;
		buf[idx] |= rpc.msr_arg0;
	}

	return (true);
}

static bool
genoa_dxio_version_at_least(const genoa_iodie_t *iodie,
    const uint32_t major, const uint32_t minor)
{
	return (iodie->gi_dxio_fw[0] > major ||
	    (iodie->gi_dxio_fw[0] == major && iodie->gi_dxio_fw[1] >= minor));
}

static void
genoa_dxio_rpc(genoa_iodie_t *iodie, genoa_dxio_rpc_t *dxio_rpc)
{
	genoa_smu_rpc_t smu_rpc = { 0 };

	smu_rpc.msr_req = GENOA_SMU_OP_DXIO;
	smu_rpc.msr_arg0 = dxio_rpc->mdr_req;
	smu_rpc.msr_arg1 = dxio_rpc->mdr_engine;
	smu_rpc.msr_arg2 = dxio_rpc->mdr_arg0;
	smu_rpc.msr_arg3 = dxio_rpc->mdr_arg1;
	smu_rpc.msr_arg4 = dxio_rpc->mdr_arg2;
	smu_rpc.msr_arg5 = dxio_rpc->mdr_arg3;

	genoa_smu_rpc(iodie, &smu_rpc);

	dxio_rpc->mdr_smu_resp = smu_rpc.msr_resp;
	if (smu_rpc.msr_resp == GENOA_SMU_RPC_OK) {
		dxio_rpc->mdr_dxio_resp = smu_rpc.msr_arg0;
		dxio_rpc->mdr_engine = smu_rpc.msr_arg1;
		dxio_rpc->mdr_arg0 = smu_rpc.msr_arg2;
		dxio_rpc->mdr_arg1 = smu_rpc.msr_arg3;
		dxio_rpc->mdr_arg2 = smu_rpc.msr_arg4;
		dxio_rpc->mdr_arg3 = smu_rpc.msr_arg5;
	}
}

static bool
genoa_dxio_rpc_get_version(genoa_iodie_t *iodie, uint32_t *major,
    uint32_t *minor)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_GET_VERSION;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Get Version RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x", rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	*major = rpc.mdr_arg0;
	*minor = rpc.mdr_arg1;

	return (true);
}

static bool
genoa_dxio_rpc_init(genoa_iodie_t *iodie)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_INIT;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Init RPC Failed: SMU 0x%x, DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_set_var(genoa_iodie_t *iodie, uint32_t var, uint32_t val)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_SET_VARIABLE;
	rpc.mdr_engine = var;
	rpc.mdr_arg0 = val;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == GENOA_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp == GENOA_DXIO_RPC_MBOX_IDLE)) {
		cmn_err(CE_WARN, "DXIO Set Variable Failed: Var: 0x%x, "
		    "Val: 0x%x, SMU 0x%x, DXIO: 0x%x", var, val,
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_pcie_poweroff_config(genoa_iodie_t *iodie, uint8_t delay,
    bool disable_prep)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_SET_VARIABLE;
	rpc.mdr_engine = GENOA_DXIO_VAR_PCIE_POWER_OFF_DELAY;
	rpc.mdr_arg0 = delay;
	rpc.mdr_arg1 = disable_prep ? 1 : 0;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == GENOA_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp == GENOA_DXIO_RPC_MBOX_IDLE)) {
		cmn_err(CE_WARN, "DXIO Set PCIe Power Off Config Failed: "
		    "Delay: 0x%x, Disable Prep: 0x%x, SMU 0x%x, DXIO: 0x%x",
		    delay, disable_prep, rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_clock_gating(genoa_iodie_t *iodie, uint8_t mask, uint8_t val)
{
	genoa_dxio_rpc_t rpc = { 0 };

	/*
	 * The mask and val are only allowed to be 7-bit values.
	 */
	VERIFY0(mask & 0x80);
	VERIFY0(val & 0x80);
	rpc.mdr_req = GENOA_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = GENOA_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = GENOA_DXIO_RT_CONF_CLOCK_GATE;
	rpc.mdr_arg1 = mask;
	rpc.mdr_arg2 = val;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Clock Gating Failed: SMU 0x%x, "
		    "DXIO: 0x%x", rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

/*
 * Currently there are no capabilities defined, which makes it hard for us to
 * know the exact command layout here. The only thing we know is safe is that
 * it's all zeros, though it probably otherwise will look like
 * GENOA_DXIO_OP_LOAD_DATA.
 */
static bool
genoa_dxio_rpc_load_caps(genoa_iodie_t *iodie)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_LOAD_CAPS;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Load Caps Failed: SMU 0x%x, DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_load_data(genoa_iodie_t *iodie, uint32_t type,
    uint64_t phys_addr, uint32_t len, uint32_t mystery)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_LOAD_DATA;
	rpc.mdr_engine = (uint32_t)(phys_addr >> 32);
	rpc.mdr_arg0 = phys_addr & 0xffffffff;
	rpc.mdr_arg1 = len / 4;
	rpc.mdr_arg2 = mystery;
	rpc.mdr_arg3 = type;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Load Data Failed: Heap: 0x%x, PA: "
		    "0x%lx, Len: 0x%x, SMU 0x%x, DXIO: 0x%x", type, phys_addr,
		    len, rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_conf_training(genoa_iodie_t *iodie, uint32_t reset_time,
    uint32_t rx_poll, uint32_t l0_poll)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = GENOA_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = GENOA_DXIO_RT_CONF_PCIE_TRAIN;
	rpc.mdr_arg1 = reset_time;
	rpc.mdr_arg2 = rx_poll;
	rpc.mdr_arg3 = l0_poll;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == GENOA_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK)) {
		cmn_err(CE_WARN, "DXIO Conf. PCIe Training RPC Failed: "
		    "SMU 0x%x, DXIO: 0x%x", rpc.mdr_smu_resp,
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
genoa_dxio_rpc_misc_rt_conf(genoa_iodie_t *iodie, uint32_t code, bool state)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = GENOA_DXIO_ENGINE_NONE;
	rpc.mdr_arg0 = GENOA_DXIO_RT_SET_CONF;
	rpc.mdr_arg1 = code;
	rpc.mdr_arg2 = state ? 1 : 0;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == GENOA_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK)) {
		cmn_err(CE_WARN, "DXIO Set Misc. rt conf failed: Code: 0x%x, "
		    "Val: 0x%x, SMU 0x%x, DXIO: 0x%x", code, state,
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_sm_start(genoa_iodie_t *iodie)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_START_SM;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_sm_resume(genoa_iodie_t *iodie)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_RESUME_SM;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static bool
genoa_dxio_rpc_sm_reload(genoa_iodie_t *iodie)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_RELOAD_SM;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Reload RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}


static bool
genoa_dxio_rpc_sm_getstate(genoa_iodie_t *iodie, genoa_dxio_reply_t *smp)
{
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_GET_SM_STATE;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	smp->gdr_type = bitx64(rpc.mdr_engine, 7, 0);
	smp->gdr_nargs = bitx64(rpc.mdr_engine, 15, 8);
	smp->gdr_arg0 = rpc.mdr_arg0;
	smp->gdr_arg1 = rpc.mdr_arg1;
	smp->gdr_arg2 = rpc.mdr_arg2;
	smp->gdr_arg3 = rpc.mdr_arg3;

	return (true);
}

/*
 * Retrieve the current engine data from DXIO.
 */
static bool
genoa_dxio_rpc_retrieve_engine(genoa_iodie_t *iodie)
{
	genoa_mpio_config_t *conf = &iodie->gi_dxio_conf;
	genoa_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = GENOA_DXIO_OP_GET_ENGINE_CFG;
	rpc.mdr_engine = (uint32_t)(conf->gmc_pa >> 32);
	rpc.mdr_arg0 = conf->gmc_pa & 0xffffffff;
	rpc.mdr_arg1 = conf->gmc_alloc_len / 4;

	genoa_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != GENOA_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != GENOA_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Retrieve Engine Failed: SMU 0x%x, "
		    "DXIO: 0x%x", rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (false);
	}

	return (true);
}

static int
genoa_dump_versions(genoa_iodie_t *iodie, void *arg)
{
	uint8_t maj, min, patch;
	uint32_t dxmaj, dxmin;
	genoa_soc_t *soc = iodie->gi_soc;

	if (genoa_smu_rpc_get_version(iodie, &maj, &min, &patch)) {
		cmn_err(CE_CONT, "?Socket %u SMU Version: %u.%u.%u\n",
		    soc->gs_socno, maj, min, patch);
		iodie->gi_smu_fw[0] = maj;
		iodie->gi_smu_fw[1] = min;
		iodie->gi_smu_fw[2] = patch;
	} else {
		cmn_err(CE_NOTE, "Socket %u: failed to read SMU version",
		    soc->gs_socno);
	}

	if (genoa_dxio_rpc_get_version(iodie, &dxmaj, &dxmin)) {
		cmn_err(CE_CONT, "?Socket %u DXIO Version: %u.%u\n",
		    soc->gs_socno, dxmaj, dxmin);
		iodie->gi_dxio_fw[0] = dxmaj;
		iodie->gi_dxio_fw[1] = dxmin;
	} else {
		cmn_err(CE_NOTE, "Socket %u: failed to read DXIO version",
		    soc->gs_socno);
	}

	return (0);
}

static void
genoa_ccx_init_core(genoa_ccx_t *ccx, uint8_t lidx, uint8_t pidx)
{
	smn_reg_t reg;
	uint32_t val;
	genoa_core_t *core = &ccx->gcx_cores[lidx];
	genoa_ccd_t *ccd = ccx->gcx_ccd;
	genoa_iodie_t *iodie = ccd->gcd_iodie;

	core->gc_ccx = ccx;
	core->gc_physical_coreno = pidx;

	reg = genoa_core_reg(core, D_SCFCTP_PMREG_INITPKG0);
	val = genoa_core_read(core, reg);
	VERIFY3U(val, !=, 0xffffffffU);

	core->gc_logical_coreno = SCFCTP_PMREG_INITPKG0_GET_LOG_CORE(val);

	VERIFY3U(SCFCTP_PMREG_INITPKG0_GET_PHYS_CORE(val), ==, pidx);
	VERIFY3U(SCFCTP_PMREG_INITPKG0_GET_PHYS_CCX(val), ==,
	    ccx->gcx_physical_cxno);
	VERIFY3U(SCFCTP_PMREG_INITPKG0_GET_PHYS_DIE(val), ==,
	    ccx->gcx_ccd->gcd_physical_dieno);

	core->gc_nthreads = SCFCTP_PMREG_INITPKG0_GET_SMTEN(val) + 1;
	VERIFY3U(core->gc_nthreads, <=, GENOA_MAX_THREADS_PER_CORE);

	for (uint8_t thr = 0; thr < core->gc_nthreads; thr++) {
		uint32_t apicid = 0;
		genoa_thread_t *thread = &core->gc_threads[thr];

		thread->gt_threadno = thr;
		thread->gt_core = core;
		nthreads++;

		/*
		 * You may be wondering why we don't use the contents of
		 * DF::CcdUnitIdMask here to determine the number of bits at
		 * each level.  There are two reasons, one simple and one not:
		 *
		 * - First, it's not correct.  The UnitId masks describe (*)
		 *   the physical ID spaces, which are distinct from how APIC
		 *   IDs are computed.  APIC IDs depend on the number of each
		 *   component that are *actually present*, rounded up to the
		 *   next power of 2 at each component.  For example, if there
		 *   are 4 CCDs, there will be 2 bits in the APIC ID for the
		 *   logical CCD number, even though representing the UnitId
		 *   on Genoa requires 3 bits for the CCD.  No, we don't know
		 *   why this is so; it would certainly have been simpler to
		 *   always use the physical ID to compute the initial APIC ID.
		 * - Second, not only are APIC IDs not UnitIds, there is nothing
		 *   documented that does consume UnitIds.  We are given a nice
		 *   discussion of what they are and this lovingly detailed way
		 *   to discover how to compute them, but so far as I have been
		 *   able to tell, neither UnitIds nor the closely related
		 *   CpuIds are ever used.  If we later find that we do need
		 *   these identifiers, additional code to construct them based
		 *   on this discovery mechanism should be added.
		 */
		apicid = iodie->gi_soc->gs_socno;
		apicid <<= highbit(iodie->gi_soc->gs_ndies - 1);
		apicid |= 0;	/* XXX multi-die SOCs not supported here */
		apicid <<= highbit(iodie->gi_nccds - 1);
		apicid |= ccd->gcd_logical_dieno;
		apicid <<= highbit(ccd->gcd_nccxs - 1);
		apicid |= ccx->gcx_logical_cxno;
		apicid <<= highbit(ccx->gcx_ncores - 1);
		apicid |= core->gc_logical_coreno;
		apicid <<= highbit(core->gc_nthreads - 1);
		apicid |= thr;

		thread->gt_apicid = (apicid_t)apicid;
	}
}

static void
genoa_ccx_init_soc(genoa_soc_t *soc)
{
	const genoa_fabric_t *fabric = soc->gs_fabric;
	genoa_iodie_t *iodie = &soc->gs_iodies[0];

	/*
	 * We iterate over the physical CCD space; population of that
	 * space may be sparse.  Keep track of the logical CCD index in
	 * lccd; ccdpno is the physical CCD index we're considering.
	 */
	for (uint8_t ccdpno = 0, lccd = 0;
	    ccdpno < GENOA_MAX_CCDS_PER_IODIE; ccdpno++) {
		uint8_t core_shift, pcore, lcore, pccx;
		smn_reg_t reg;
		uint32_t val;
		uint32_t cores_enabled;
		genoa_ccd_t *ccd = &iodie->gi_ccds[lccd];
		genoa_ccx_t *ccx = &ccd->gcd_ccxs[0];

		/*
		 * The CCM is part of the IO die, not the CCD itself.
		 * If it is disabled, we skip this CCD index as even if
		 * it exists nothing can reach it.
		 */
		val = genoa_df_read32(iodie,
		    GENOA_DF_FIRST_CCM_INST_ID + ccdpno, DF_FBIINFO0);

		VERIFY3U(DF_FBIINFO0_GET_TYPE(val), ==, DF_TYPE_CCM);
		if (DF_FBIINFO0_V3_GET_ENABLED(val) == 0)
			continue;

		/*
		 * At least some of the time, a CCM will be enabled
		 * even if there is no corresponding CCD.  To avoid
		 * a possibly invalid read (see genoa_fabric_topo_init()
		 * comments), we also check whether any core is enabled
		 * on this CCD.
		 *
		 * XXX reduce magic
		 */
		val = genoa_df_bcast_read32(iodie,
		    (ccdpno < 4) ? DF_PHYS_CORE_EN0_V4 : ((ccdpno < 8) ?
		    DF_PHYS_CORE_EN1_V4 : DF_PHYS_CORE_EN2_V4));
		core_shift = (ccdpno & 3) * GENOA_MAX_CORES_PER_CCX *
		    GENOA_MAX_CCXS_PER_CCD;
		cores_enabled = bitx32(val, core_shift + 7, core_shift);

		if (cores_enabled == 0)
			continue;

		VERIFY3U(lccd, <, GENOA_MAX_CCDS_PER_IODIE);
		ccd->gcd_iodie = iodie;
		ccd->gcd_logical_dieno = lccd++;
		ccd->gcd_physical_dieno = ccdpno;
		ccd->gcd_ccm_comp_id = GENOA_DF_FIRST_CCM_COMP_ID + ccdpno;
		ccd->gcd_ccm_fabric_id = ccd->gcd_ccm_comp_id |
		    (iodie->gi_node_id << fabric->gf_node_shift);

		/* XXX avoid panicking on bad data from firmware */
		reg = genoa_ccd_reg(ccd, D_SMUPWR_CCD_DIE_ID);
		val = genoa_ccd_read(ccd, reg);
		VERIFY3U(val, ==, ccdpno);

		reg = genoa_ccd_reg(ccd, D_SMUPWR_THREAD_CFG);
		val = genoa_ccd_read(ccd, reg);
		ccd->gcd_nccxs = SMUPWR_THREAD_CFG_GET_COMPLEX_COUNT(val) + 1;
		VERIFY3U(ccd->gcd_nccxs, <=, GENOA_MAX_CCXS_PER_CCD);

		if (ccd->gcd_nccxs == 0) {
			cmn_err(CE_NOTE, "CCD 0x%x: no CCXs reported",
			    ccd->gcd_physical_dieno);
			continue;
		}

		/*
		 * Make sure that the CCD's local understanding of
		 * enabled cores matches what we found earlier through
		 * the DF.  A mismatch here is a firmware bug; XXX and
		 * if that happens?
		 */
		reg = genoa_ccd_reg(ccd, D_SMUPWR_CORE_EN);
		val = genoa_ccd_read(ccd, reg);
		VERIFY3U(SMUPWR_CORE_EN_GET(val), ==, cores_enabled);

		/*
		 * XXX While we know there is only ever 1 CCX per Genoa CCD,
		 * DF::CCXEnable allows for 2 because the DFv3 implementation
		 * is shared with Rome, which has up to 2 CCXs per CCD.
		 * Although we know we only ever have 1 CCX, we don't,
		 * strictly, know that the CCX is always physical index 0.
		 * Here we assume it, but we probably want to change the
		 * GENOA_MAX_xxx_PER_yyy so that they reflect the size of the
		 * physical ID spaces rather than the maximum logical entity
		 * counts.  Doing so would accommodate a part that has a single
		 * CCX per CCD, but at index 1.
		 */
		ccx->gcx_ccd = ccd;
		ccx->gcx_logical_cxno = 0;
		ccx->gcx_physical_cxno = pccx = 0;

		/*
		 * All the cores on the CCD will (should) return the
		 * same values in PMREG_INITPKG0 and PMREG_INITPKG7.
		 * The catch is that we have to read them from a core
		 * that exists or we get all-1s.  Use the mask of
		 * cores enabled on this die that we already computed
		 * to find one to read from, then bootstrap into the
		 * core enumeration.  XXX At some point we probably
		 * should do away with all this cross-checking and
		 * choose something to trust.
		 */
		for (pcore = 0;
		    (cores_enabled & (1 << pcore)) == 0 &&
		    pcore < GENOA_MAX_CORES_PER_CCX; pcore++)
			;
		VERIFY3U(pcore, <, GENOA_MAX_CORES_PER_CCX);

		reg = SCFCTP_PMREG_INITPKG7(ccdpno, pccx, pcore);
		val = genoa_smn_read(iodie, reg);
		VERIFY3U(val, !=, 0xffffffffU);

		ccx->gcx_ncores = SCFCTP_PMREG_INITPKG7_GET_N_CORES(val) + 1;
		iodie->gi_nccds = SCFCTP_PMREG_INITPKG7_GET_N_DIES(val) + 1;

		for (pcore = 0, lcore = 0;
		    pcore < GENOA_MAX_CORES_PER_CCX; pcore++) {
			if ((cores_enabled & (1 << pcore)) == 0)
				continue;
			genoa_ccx_init_core(ccx, lcore, pcore);
			++lcore;
		}

		VERIFY3U(lcore, ==, ccx->gcx_ncores);
	}
}

static bool
genoa_smu_features_init(genoa_iodie_t *iodie)
{
	genoa_smu_rpc_t rpc = { 0 };
	genoa_soc_t *soc = iodie->gi_soc;

	/*
	 * Not all combinations of SMU features will result in correct system
	 * behavior, so we therefore err on the side of matching stock platform
	 * enablement -- even where that means enabling features with unknown
	 * functionality.
	 */
	uint32_t features = GENOA_SMU_FEATURE_DATA_CALCULATION |
	    GENOA_SMU_FEATURE_THERMAL_DESIGN_CURRENT |
	    GENOA_SMU_FEATURE_THERMAL |
	    GENOA_SMU_FEATURE_PRECISION_BOOST_OVERDRIVE |
	    GENOA_SMU_FEATURE_ELECTRICAL_DESIGN_CURRENT |
	    GENOA_SMU_FEATURE_CSTATE_BOOST |
	    GENOA_SMU_FEATURE_PROCESSOR_THROTTLING_TEMPERATURE |
	    GENOA_SMU_FEATURE_CORE_CLOCK_DPM |
	    GENOA_SMU_FEATURE_FABRIC_CLOCK_DPM |
	    GENOA_SMU_FEATURE_XGMI_DYNAMIC_LINK_WIDTH_MANAGEMENT |
	    GENOA_SMU_FEATURE_DIGITAL_LDO |
	    GENOA_SMU_FEATURE_SOCCLK_DEEP_SLEEP |
	    GENOA_SMU_FEATURE_LCLK_DEEP_SLEEP |
	    GENOA_SMU_FEATURE_SYSHUBCLK_DEEP_SLEEP |
	    GENOA_SMU_FEATURE_CLOCK_GATING |
	    GENOA_SMU_FEATURE_DYNAMIC_LDO_DROPOUT_LIMITER |
	    GENOA_SMU_FEATURE_DYNAMIC_VID_OPTIMIZER |
	    GENOA_SMU_FEATURE_AGE;

	rpc.msr_req = GENOA_SMU_OP_ENABLE_FEATURE;
	rpc.msr_arg0 = features;

	genoa_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != GENOA_SMU_RPC_OK) {
		cmn_err(CE_WARN,
		    "Socket %u: SMU Enable Features RPC Failed: features: "
		    "0x%x, SMU 0x%x", soc->gs_socno, features, rpc.msr_resp);
	} else {
		cmn_err(CE_CONT, "?Socket %u SMU features 0x%08x enabled\n",
		    soc->gs_socno, features);
	}

	return (rpc.msr_resp == GENOA_SMU_RPC_OK);
}

/*
 * XXX: Copied from <sys/pci_impl.h> to avoid clashing memlist_insert defs
 */
#define	PCI_CONFADD		0xcf8
#define	PCI_CONFDATA		0xcfc
#define	PCI_CONE		0x80000000
/*
 * Like PCI_CADDR1, but allows for access to extended configuration space.
 * If `DF::CoreMasterAccessCtrl[EnableCf8ExtCfg]` is enabled, the usually
 * reserved bits 27:24 are set to bits 11:8 of the register offset.
 */
#define	PCI_CADDR1_EXT(b, d, f, r) \
		(PCI_CONE | ((((r) >> 8) & 0xf) << 24) | (((b) & 0xff) << 16) \
			    | (((d & 0x1f)) << 11) | (((f) & 0x7) << 8) \
			    | ((r) & 0xfc))

static void
genoa_df_mech1_write32(df_reg_def_t reg, uint32_t val)
{
	outl(PCI_CONFADD, PCI_CADDR1_EXT(AMDZEN_DF_BUSNO,
	    AMDZEN_DF_FIRST_DEVICE, reg.drd_func, reg.drd_reg));
	outl(PCI_CONFDATA, val);
}

/*
 * Genoa seems to require that whatever address we set for PCI MMIO
 * access (via `Core::X86::Msr::MmioCfgBaseAddr`) matches DF registers
 * `DF::MmioPciCfg{Base,Limit}Addr{,Ext}` as set by the firmware on
 * startup (see `APCB_TOKEN_UID_DF_PCI_MMIO{,_HI}_BASE` and
 * `APCB_TOKEN_UID_DF_PCI_MMIO_SIZE`).
 * But rather than require some fixed address in either the firmware or
 * the OS, we'll update the DF registers to match the address we've
 * chosen. This does present a bit of a chicken-and-egg problem since
 * we've not setup PCIe configuration space yet, so instead we resort to
 * the classic PCI Configuration Mechanism #1 via x86 I/O ports.
 */
static void
genoa_df_set_mmio_pci_cfg_space(uint64_t ecam_base)
{
	uint32_t val;
	uint64_t ecam_limit = ecam_base + PCIE_CFGSPACE_SIZE - (1*1024*1024);

	val = DF_MMIO_PCI_BASE_V4_SET_EN(0, 1);
	val = DF_MMIO_PCI_BASE_V4_SET_ADDR(val,
	    ((uint32_t)ecam_base) >> DF_MMIO_PCI_BASE_ADDR_SHIFT);
	genoa_df_mech1_write32(DF_MMIO_PCI_BASE_V4, val);

	val = DF_MMIO_PCI_BASE_EXT_V4_SET_ADDR(0, ecam_base >> 32);
	genoa_df_mech1_write32(DF_MMIO_PCI_BASE_EXT_V4, val);

	val = DF_MMIO_PCI_LIMIT_V4_SET_ADDR(0,
	    ((uint32_t)ecam_limit) >> DF_MMIO_PCI_LIMIT_ADDR_SHIFT);
	genoa_df_mech1_write32(DF_MMIO_PCI_LIMIT_V4, val);

	val = DF_MMIO_PCI_LIMIT_EXT_V4_SET_ADDR(0, ecam_limit >> 32);
	genoa_df_mech1_write32(DF_MMIO_PCI_LIMIT_EXT_V4, val);
}

/*
 * Right now we're running on the boot CPU. We know that a single socket has to
 * be populated. Our job is to go through and determine what the rest of the
 * topology of this system looks like in terms of the data fabric, north
 * bridges, and related. We can rely on the DF instance 0/18/0 to exist;
 * however, that's it.
 *
 * An important rule of discovery here is that we should not rely on invalid PCI
 * reads. We should be able to bootstrap from known good data and what the
 * actual SoC has discovered here rather than trying to fill that in ourselves.
 */
void
genoa_fabric_topo_init(void)
{
	uint8_t nsocs;
	uint32_t syscfg, syscomp, fidmask;
	genoa_fabric_t *fabric = &genoa_fabric;

	PRM_POINT("genoa_fabric_topo_init() starting...");

	/*
	 * Before we can do anything else, we must set up PCIe ECAM.  We locate
	 * this region beyond either the end of DRAM or the IOMMU hole,
	 * whichever is higher.  The remainder of the 64-bit MMIO space is
	 * available for allocation to IOMSs (for e.g. PCIe devices).
	 */
	fabric->gf_tom = MSR_AMD_TOM_MASK(rdmsr(MSR_AMD_TOM));
	fabric->gf_tom2 = MSR_AMD_TOM2_MASK(rdmsr(MSR_AMD_TOM2));

	fabric->gf_ecam_base = P2ROUNDUP(MAX(fabric->gf_tom2,
	    GENOA_PHYSADDR_IOMMU_HOLE_END), PCIE_CFGSPACE_ALIGN);
	fabric->gf_mmio64_base = fabric->gf_ecam_base + PCIE_CFGSPACE_SIZE;

	genoa_df_set_mmio_pci_cfg_space(fabric->gf_ecam_base);
	pcie_cfgspace_init();

	syscfg = genoa_df_early_read32(DF_SYSCFG_V4);
	syscomp = genoa_df_early_read32(DF_COMPCNT_V4);
	nsocs = DF_SYSCFG_V4_GET_OTHER_SOCK(syscfg) + 1;

	/*
	 * These are used to ensure that we're on a platform that matches our
	 * expectations.
	 */
	VERIFY3U(nsocs, ==, DF_COMPCNT_V4_GET_PIE(syscomp));
	VERIFY3U(nsocs * GENOA_IOMS_PER_IODIE, ==,
	    DF_COMPCNT_V4_GET_IOM(syscomp));
	VERIFY3U(nsocs * GENOA_IOMS_PER_IODIE, ==,
	    DF_COMPCNT_V4_GET_IOS(syscomp));

	/*
	 * Gather the register masks for decoding global fabric IDs into local
	 * instance IDs.
	 */
	fidmask = genoa_df_early_read32(DF_FIDMASK0_V4);
	fabric->gf_node_mask = DF_FIDMASK0_V3P5_GET_NODE_MASK(fidmask);
	fabric->gf_comp_mask = DF_FIDMASK0_V3P5_GET_COMP_MASK(fidmask);

	fidmask = genoa_df_early_read32(DF_FIDMASK1_V4);
	fabric->gf_node_shift = DF_FIDMASK1_V3P5_GET_NODE_SHIFT(fidmask);

	fabric->gf_nsocs = nsocs;
	for (uint8_t socno = 0; socno < nsocs; socno++) {
		uint32_t busno, nodeid;
		const df_reg_def_t rd = DF_SYSCFG_V4;
		genoa_soc_t *soc = &fabric->gf_socs[socno];
		genoa_iodie_t *iodie = &soc->gs_iodies[0];

		soc->gs_socno = socno;
		soc->gs_ndies = GENOA_FABRIC_MAX_DIES_PER_SOC;
		soc->gs_fabric = fabric;
		iodie->gi_dfno = AMDZEN_DF_FIRST_DEVICE + socno;

		nodeid = pci_getl_func(AMDZEN_DF_BUSNO, iodie->gi_dfno,
		    rd.drd_func, rd.drd_reg);
		iodie->gi_node_id = DF_SYSCFG_V4_GET_NODE_ID(nodeid);
		iodie->gi_soc = soc;

		if (iodie->gi_node_id == 0) {
			iodie->gi_flags |= GENOA_IODIE_F_PRIMARY;
		}

		/*
		 * XXX Because we do not know the circumstances all these locks
		 * will be used during early initialization, set these to be
		 * spin locks for the moment.
		 */
		mutex_init(&iodie->gi_df_ficaa_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->gi_smn_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->gi_smu_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));

		busno = genoa_df_bcast_read32(iodie, DF_CFG_ADDR_CTL_V4);
		iodie->gi_smn_busno = DF_CFG_ADDR_CTL_GET_BUS_NUM(busno);

		iodie->gi_nioms = GENOA_IOMS_PER_IODIE;
		fabric->gf_total_ioms += iodie->gi_nioms;
		for (uint8_t iomsno = 0; iomsno < iodie->gi_nioms; iomsno++) {
			uint32_t val;

			genoa_ioms_t *ioms = &iodie->gi_ioms[iomsno];

			ioms->gio_num = iomsno;
			ioms->gio_iodie = iodie;

			/*
			 * Determine the Instance, Component & Fabric IDs for
			 * the corresponding IOM and IOS instances.
			 *
			 * XXX: Verify
			 * 	IO[MS]0 => NBIO 0, IOHC 0
			 * 	IO[MS]1 => NBIO 0, IOHC 1
			 * 	IO[MS]2 => NBIO 1, IOHC 0
			 * 	IO[MS]3 => NBIO 1, IOHC 1
			 *
			 * XXX: Just find the FabricId via
			 * DF::FabricBlockInstanceInformation3_CSNCSPIEALLM and
			 * DF::FabricBlockInstanceInformation3_IOS
			 */

			ioms->gio_iom_inst_id = GENOA_DF_FIRST_IOM_INST_ID
			    + iomsno;
			/* XXX: don't seem to need the IOM's FabricID ? */
			ioms->gio_iom_comp_id = GENOA_DF_FIRST_IOM_COMP_ID
			    + iomsno;
			ioms->gio_iom_fabric_id = ioms->gio_iom_comp_id |
			    (iodie->gi_node_id << fabric->gf_node_shift);

			ioms->gio_ios_inst_id = GENOA_DF_FIRST_IOS_INST_ID
			    + iomsno;
			ioms->gio_ios_comp_id = GENOA_DF_FIRST_IOS_COMP_ID
			    + iomsno;
			ioms->gio_ios_fabric_id = ioms->gio_ios_comp_id |
			    (iodie->gi_node_id << fabric->gf_node_shift);

			/*
			 * Grab the bus number for the IO Link from the IOS
			 */
			val = genoa_df_read32(iodie, ioms->gio_ios_inst_id,
			    DF_CFG_ADDR_CTL_V4);
			ioms->gio_pci_busno = DF_CFG_ADDR_CTL_GET_BUS_NUM(val);

			/*
			 * Only IOMS 0 has a WAFL port.
			 *
			 * XXX: Verify
			 */
			ioms->gio_npcie_cores = genoa_nbio_n_pcie_cores(iomsno);
			if (iomsno == GENOA_IOMS_HAS_WAFL) {
				ioms->gio_flags |= GENOA_IOMS_F_HAS_WAFL;
			}
			ioms->gio_nnbifs = GENOA_IOMS_MAX_NBIF;

			/*
			 * Only IOMS 3 has an FCH.
			 *
			 * XXX: Don't need to hardcode?
			 *     DF::SpecialSysFunctionFabricID2[FchIOSFabricID]
			 */
			if (iomsno == GENOA_IOMS_HAS_FCH) {
				ioms->gio_flags |= GENOA_IOMS_F_HAS_FCH;
			}

			genoa_fabric_ioms_pcie_init(ioms);
			genoa_fabric_ioms_nbif_init(ioms);
		}

		/*
		 * In order to guarantee that we can safely perform SMU and DXIO
		 * functions once we have returned (and when we go to read the
		 * brand string for the CCXs even before then), we go through
		 * now and capture firmware versions.
		 */
		VERIFY0(genoa_dump_versions(iodie, NULL));

		genoa_ccx_init_soc(soc);
		if (!genoa_smu_rpc_read_brand_string(iodie, soc->gs_brandstr,
		    sizeof (soc->gs_brandstr))) {
			soc->gs_brandstr[0] = '\0';
		}

		if (!genoa_smu_rpc_read_dpm_weights(iodie,
		    iodie->gi_dpm_weights, sizeof (iodie->gi_dpm_weights))) {
			/*
			 * XXX It's unclear whether continuing is wise.
			 */
			cmn_err(CE_WARN, "SMU: failed to retrieve DPM weights");
			bzero(iodie->gi_dpm_weights,
			    sizeof (iodie->gi_dpm_weights));
		}

		/*
		 * We want to enable SMU features now because it will enable
		 * dynamic frequency scaling -- which in turn makes the rest
		 * of the boot much, much faster.
		 */
		VERIFY(genoa_smu_features_init(iodie));
	}

	if (nthreads > NCPU) {
		cmn_err(CE_WARN, "%d CPUs found but only %d supported",
		    nthreads, NCPU);
		nthreads = NCPU;
	}
	boot_max_ncpus = max_ncpus = boot_ncpus = nthreads;
}

/*
 * The IOHC needs our help to know where the top of memory is. This is
 * complicated for a few reasons. Right now we're relying on where TOM and TOM2
 * have been programmed by the PSP to determine that. The biggest gotcha here is
 * the secondary MMIO hole that leads to us needing to actually have a 3rd
 * register in the IOHC for indicating DRAM/MMIO splits.
 */
static int
genoa_fabric_init_tom(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	uint64_t tom2, tom3;
	genoa_fabric_t *fabric = ioms->gio_iodie->gi_soc->gs_fabric;

	/*
	 * This register is a little funky. Bit 32 of the address has to be
	 * specified in bit 0. Otherwise, bits 31:23 are the limit.
	 */
	val = pci_getl_func(ioms->gio_pci_busno, 0, 0, IOHC_TOM);
	if (bitx64(fabric->gf_tom, 32, 32) != 0) {
		val = IOHC_TOM_SET_BIT32(val, 1);
	}

	val = IOHC_TOM_SET_TOM(val, bitx64(fabric->gf_tom, 31, 23));
	pci_putl_func(ioms->gio_pci_busno, 0, 0, IOHC_TOM, val);

	if (fabric->gf_tom2 == 0) {
		return (0);
	}

	if (fabric->gf_tom2 > GENOA_PHYSADDR_IOMMU_HOLE_END) {
		tom2 = GENOA_PHYSADDR_IOMMU_HOLE;
		tom3 = fabric->gf_tom2 - 1;
	} else {
		tom2 = fabric->gf_tom2;
		tom3 = 0;
	}

	/*
	 * Write the upper register before the lower so we don't accidentally
	 * enable it in an incomplete fashion.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_DRAM_TOM2_HI, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_HI_SET_TOM2(val, bitx64(tom2, 40, 32));
	genoa_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOHC_DRAM_TOM2_LOW, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_LOW_SET_EN(val, 1);
	val = IOHC_DRAM_TOM2_LOW_SET_TOM2(val, bitx64(tom2, 31, 23));
	genoa_ioms_write(ioms, reg, val);

	if (tom3 == 0) {
		return (0);
	}

	reg = genoa_ioms_reg(ioms, D_IOHC_DRAM_TOM3, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM3_SET_EN(val, 1);
	val = IOHC_DRAM_TOM3_SET_LIMIT(val, bitx64(tom3, 51, 22));
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

/*
 * We want to disable VGA and send all downstream accesses to its address range
 * to DRAM just as we do from the cores.  This requires clearing
 * IOHC::NB_PCI_ARB[VGA_HOLE]; for reasons unknown, the default here is
 * different from the other settings that typically default to VGA-off.  The
 * rest of this register has nothing to do with decoding and we leave its
 * contents alone.
 */
static int
genoa_fabric_disable_iohc_vga(genoa_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = pci_getl_func(ioms->gio_pci_busno, 0, 0, IOHC_NB_PCI_ARB);
	val = IOHC_NB_PCI_ARB_SET_VGA_HOLE(val, IOHC_NB_PCI_ARB_VGA_HOLE_RAM);
	pci_putl_func(ioms->gio_pci_busno, 0, 0, IOHC_NB_PCI_ARB, val);

	return (0);
}

/*
 * Set the IOHC PCI device's subsystem identifiers.  This could be set to the
 * baseboard's subsystem ID, but the IOHC PCI device doesn't have any
 * oxide-specific semantics so we leave it at the AMD-recommended value.  Note
 * that the POR default value is not the one AMD recommends, for whatever
 * reason.
 */
static int
genoa_fabric_init_iohc_pci(genoa_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = pci_getl_func(ioms->gio_pci_busno, 0, 0, IOHC_NB_ADAPTER_ID_W);
	val = IOHC_NB_ADAPTER_ID_W_SET_SVID(val, VENID_AMD);
	val = IOHC_NB_ADAPTER_ID_W_SET_SDID(val,
	    IOHC_NB_ADAPTER_ID_W_AMD_GENOA_IOHC);
	pci_putl_func(ioms->gio_pci_busno, 0, 0, IOHC_NB_ADAPTER_ID_W, val);

	return (0);
}

/*
 * Different parts of the IOMS need to be programmed such that they can figure
 * out if they have a corresponding FCH present on them. The FCH is only present
 * on IOMS 3. Therefore if we're on IOMS 3 we need to update various other bis
 * of the IOAGR and related; however, if we're not on IOMS 3 then we just need
 * to zero out some of this.
 */
static int
genoa_fabric_init_iohc_fch_link(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;

	reg = genoa_ioms_reg(ioms, D_IOHC_SB_LOCATION, 0);
	if ((ioms->gio_flags & GENOA_IOMS_F_HAS_FCH) != 0) {
		smn_reg_t iommureg;
		uint32_t val;

		val = genoa_ioms_read(ioms, reg);
		iommureg = genoa_ioms_reg(ioms, D_IOMMUL1_SB_LOCATION,
		    MIL1SU_IOAGR);
		genoa_ioms_write(ioms, iommureg, val);
		iommureg = genoa_ioms_reg(ioms, D_IOMMUL2_SB_LOCATION, 0);
		genoa_ioms_write(ioms, iommureg, val);
	} else {
		genoa_ioms_write(ioms, reg, 0);
	}

	return (0);
}

/*
 * For some reason the PCIe reference clock does not default to 100 MHz. We need
 * to do this ourselves. If we don't do this, PCIe will not be very happy.
 */
static int
genoa_fabric_init_pcie_refclk(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_REFCLK_MODE, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_REFCLK_MODE_SET_27MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_25MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_100MHZ(val, 1);
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

/*
 * While the value for the delay comes from the PPR, the value for the limit
 * comes from other AMD sources.
 */
static int
genoa_fabric_init_pci_to(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_PCIE_CRS_COUNT, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_PCIE_CRS_COUNT_SET_LIMIT(val, 0x262);
	val = IOHC_PCIE_CRS_COUNT_SET_DELAY(val, 0x6);
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

/*
 * Here we initialize several of the IOHC features and related vendor-specific
 * messages are all set up correctly. XXX We're using lazy defaults of what the
 * system default has historically been here for some of these. We should test
 * and forcibly disable in hardware. Probably want to manipulate
 * IOHC::PCIE_VDM_CNTL2 at some point to better figure out the VDM story. XXX
 * Also, ARI entablement is being done earlier than otherwise because we want to
 * only touch this reg in one place if we can.
 */
static int
genoa_fabric_init_iohc_features(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_FCTL, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_FCTL_SET_ARI(val, 1);
	/* XXX Wants to be IOHC_FCTL_P2P_DISABLE? */
	val = IOHC_FCTL_SET_P2P(val, IOHC_FCTL_P2P_DROP_NMATCH);
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

static int
genoa_fabric_init_arbitration_ioms(genoa_ioms_t *ioms, void *arg)
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

		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);

		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);

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

		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_TIME_LOW, i);
		genoa_ioms_write(ioms, reg, tsval);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_TIME_HI, i);
		genoa_ioms_write(ioms, reg, tsval);
	}

	/*
	 * Yes, we only set [4:0] here. I know it's odd. We're actually setting
	 * S1's only instance (0) and the first 4 of the 6 instances of S0.
	 * Apparently it's not necessary to set instances 5 and 6.
	 */
	for (uint_t i = 0; i < 4; i++) {
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_Sn_CLI_NP_DEFICIT, i);

		val = genoa_ioms_read(ioms, reg);
		val = IOHC_SION_CLI_NP_DEFICIT_SET(val,
		    IOHC_SION_CLI_NP_DEFICIT_VAL);
		genoa_ioms_write(ioms, reg, val);
	}

	/*
	 * Go back and finally set the live lock watchdog to finish off the
	 * IOHC.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_SION_LLWD_THRESH, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_SION_LLWD_THRESH_SET(val, IOHC_SION_LLWD_THRESH_VAL);
	genoa_ioms_write(ioms, reg, val);

	/*
	 * Next on our list is the IOAGR. While there are 5 entries, only 4 are
	 * ever set it seems.
	 */
	for (uint_t i = 0; i < 4; i++) {
		uint32_t tsval;

		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S1_CLIREQ_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S1_CLIREQ_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);

		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_RDRSP_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_RDRSP_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S1_RDRSP_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S1_RDRSP_BURST_HI, i);
		genoa_ioms_write(ioms, reg, IOAGR_SION_RDRSP_BURST_VAL);

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

		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_TIME_LOW, i);
		genoa_ioms_write(ioms, reg, tsval);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_TIME_HI, i);
		genoa_ioms_write(ioms, reg, tsval);
	}

	/*
	 * The IOAGR only has the watchdog.
	 */

	reg = genoa_ioms_reg(ioms, D_IOAGR_SION_LLWD_THRESH, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOAGR_SION_LLWD_THRESH_SET(val, IOAGR_SION_LLWD_THRESH_VAL);
	genoa_ioms_write(ioms, reg, val);

	/*
	 * Finally, the SDPMUX variant, which is surprisingly consistent
	 * compared to everything else to date.
	 */
	for (uint_t i = 0; i < SDPMUX_SION_MAX_ENTS; i++) {
		reg = genoa_ioms_reg(ioms,
		    D_SDPMUX_SION_S0_CLIREQ_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S0_CLIREQ_BURST_HI, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms,
		    D_SDPMUX_SION_S1_CLIREQ_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S1_CLIREQ_BURST_HI, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S0_RDRSP_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S0_RDRSP_BURST_HI, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S1_RDRSP_BURST_LOW, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S1_RDRSP_BURST_HI, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);

		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S0_CLIREQ_TIME_LOW, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_TIME_VAL);
		reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_S0_CLIREQ_TIME_HI, i);
		genoa_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_TIME_VAL);
	}

	reg = genoa_ioms_reg(ioms, D_SDPMUX_SION_LLWD_THRESH, 0);
	val = genoa_ioms_read(ioms, reg);
	val = SDPMUX_SION_LLWD_THRESH_SET(val, SDPMUX_SION_LLWD_THRESH_VAL);
	genoa_ioms_write(ioms, reg, val);

	/*
	 * XXX We probably don't need this since we don't have USB. But until we
	 * have things working and can experiment, hard to say. If someone were
	 * to use the bus, probably something we need to consider.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_USB_QOS_CTL, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_USB_QOS_CTL_SET_UNID1_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID1_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID1_ID(val, 0x30);
	val = IOHC_USB_QOS_CTL_SET_UNID0_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID0_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID0_ID(val, 0x2f);
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

static int
genoa_fabric_init_arbitration_nbif(genoa_nbif_t *nbif, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT2, 0);
	genoa_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);
	reg = genoa_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT3, 0);
	genoa_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);

	reg = genoa_nbif_reg(nbif, D_NBIF_BIFC_MISC_CTL0, 0);
	val = genoa_nbif_read(nbif, reg);
	val = NBIF_BIFC_MISC_CTL0_SET_PME_TURNOFF(val,
	    NBIF_BIFC_MISC_CTL0_PME_TURNOFF_FW);
	genoa_nbif_write(nbif, reg, val);

	return (0);
}

/*
 * This sets up a bunch of hysteresis and port controls around the SDP, DMA
 * actions, and ClkReq. In general, these values are what we're told to set them
 * to in the PPR. Note, there is no need to change
 * IOAGR::IOAGR_SDP_PORT_CONTROL, which is why it is missing. The SDPMUX does
 * not have an early wake up register.
 */
static int
genoa_fabric_init_sdp_control(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_SDP_PORT_CTL, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_SDP_PORT_CTL_SET_PORT_HYSTERESIS(val, 0xff);
	genoa_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOHC_SDP_EARLY_WAKE_UP, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_SDP_EARLY_WAKE_UP_SET_HOST_ENABLE(val, 0xffff);
	val = IOHC_SDP_EARLY_WAKE_UP_SET_DMA_ENABLE(val, 0x1);
	genoa_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOAGR_EARLY_WAKE_UP, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOAGR_EARLY_WAKE_UP_SET_DMA_ENABLE(val, 0x1);
	genoa_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_SDPMUX_SDP_PORT_CTL, 0);
	val = genoa_ioms_read(ioms, reg);
	val = SDPMUX_SDP_PORT_CTL_SET_HOST_ENABLE(val, 0xffff);
	val = SDPMUX_SDP_PORT_CTL_SET_DMA_ENABLE(val, 0x1);
	val = SDPMUX_SDP_PORT_CTL_SET_PORT_HYSTERESIS(val, 0xff);
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

/*
 * XXX This bit of initialization is both strange and not very well documented.
 * This is a bit weird where by we always set this on nbif0 across all IOMS
 * instances; however, we only do it on NBIF1 for IOMS 0/1. Not clear why that
 * is. There are a bunch of things that don't quite make sense about being
 * specific to the syshub when generally we expect the one we care about to
 * actually be on IOMS 3.
 */
static int
genoa_fabric_init_nbif_syshub_dma(genoa_nbif_t *nbif, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * This register, like all SYSHUBMM registers, has no instance on NBIF2.
	 */
	if (nbif->gn_nbifno > 1 ||
	    (nbif->gn_nbifno > 0 && nbif->gn_ioms->gio_num > 1)) {
		return (0);
	}
	reg = genoa_nbif_reg(nbif, D_NBIF_ALT_BGEN_BYP_SOC, 0);
	val = genoa_nbif_read(nbif, reg);
	val = NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW0(val, 1);
	genoa_nbif_write(nbif, reg, val);
	return (0);
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
static int
genoa_fabric_init_ioapic(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	ASSERT3U(ARRAY_SIZE(genoa_ioapic_routes), ==, IOAPIC_NROUTES);

	for (uint_t i = 0; i < ARRAY_SIZE(genoa_ioapic_routes); i++) {
		smn_reg_t reg = genoa_ioms_reg(ioms, D_IOAPIC_ROUTE, i);
		uint32_t route = genoa_ioms_read(ioms, reg);

		route = IOAPIC_ROUTE_SET_BRIDGE_MAP(route,
		    genoa_ioapic_routes[i].mii_map);
		route = IOAPIC_ROUTE_SET_INTX_SWIZZLE(route,
		    genoa_ioapic_routes[i].mii_swiz);
		route = IOAPIC_ROUTE_SET_INTX_GROUP(route,
		    genoa_ioapic_routes[i].mii_group);

		genoa_ioms_write(ioms, reg, route);
	}

	/*
	 * The address registers are in the IOHC while the feature registers are
	 * in the IOAPIC SMN space. To ensure that the other IOAPICs can't be
	 * enabled with reset addresses, we instead lock them. XXX Should we
	 * lock primary?
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_HI, 0);
	val = genoa_ioms_read(ioms, reg);
	if ((ioms->gio_flags & GENOA_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val,
		    bitx64(GENOA_PHYSADDR_IOHC_IOAPIC, 47, 32));
	} else {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val, 0);
	}
	genoa_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_LO, 0);
	val = genoa_ioms_read(ioms, reg);
	if ((ioms->gio_flags & GENOA_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_LO_SET_ADDR(val,
		    bitx64(GENOA_PHYSADDR_IOHC_IOAPIC, 31, 8));
		val = IOHC_IOAPIC_ADDR_LO_SET_LOCK(val, 0);
		val = IOHC_IOAPIC_ADDR_LO_SET_EN(val, 1);
	} else {
		val = IOHC_IOAPIC_ADDR_LO_SET_ADDR(val, 0);
		val = IOHC_IOAPIC_ADDR_LO_SET_LOCK(val, 1);
		val = IOHC_IOAPIC_ADDR_LO_SET_EN(val, 0);
	}
	genoa_ioms_write(ioms, reg, val);

	/*
	 * Every IOAPIC requires that we enable 8-bit addressing and that it be
	 * able to generate interrupts to the FCH. The most important bit here
	 * is the secondary bit which determines whether or not this IOAPIC is
	 * subordinate to another.
	 */
	reg = genoa_ioms_reg(ioms, D_IOAPIC_FEATURES, 0);
	val = genoa_ioms_read(ioms, reg);
	if ((ioms->gio_flags & GENOA_IOMS_F_HAS_FCH) != 0) {
		val = IOAPIC_FEATURES_SET_SECONDARY(val, 0);
	} else {
		val = IOAPIC_FEATURES_SET_SECONDARY(val, 1);
	}
	val = IOAPIC_FEATURES_SET_FCH(val, 1);
	val = IOAPIC_FEATURES_SET_ID_EXT(val, 1);
	genoa_ioms_write(ioms, reg, val);

	return (0);
}

/*
 * Each IOHC has registers that can further constraion what type of PCI bus
 * numbers the IOHC itself is expecting to reply to. As such, we program each
 * IOHC with its primary bus number and enable this.
 */
static int
genoa_fabric_init_bus_num(genoa_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_BUS_NUM_CTL, 0);
	val = genoa_ioms_read(ioms, reg);
	val = IOHC_BUS_NUM_CTL_SET_EN(val, 1);
	val = IOHC_BUS_NUM_CTL_SET_BUS(val, ioms->gio_pci_busno);
	genoa_ioms_write(ioms, reg, val);

	return (0);
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
static int
genoa_fabric_init_nbif_dev_straps(genoa_nbif_t *nbif, void *arg)
{
	smn_reg_t reg;
	uint32_t intr;

	reg = genoa_nbif_reg(nbif, D_NBIF_INTR_LINE_EN, 0);
	intr = genoa_nbif_read(nbif, reg);
	for (uint8_t funcno = 0; funcno < nbif->gn_nfuncs; funcno++) {
		smn_reg_t strapreg;
		uint32_t strap;
		genoa_nbif_func_t *func = &nbif->gn_funcs[funcno];

		/*
		 * This indicates that we have a dummy function or similar. In
		 * which case there's not much to do here, the system defaults
		 * are generally what we want. XXX Kind of sort of. Not true
		 * over time.
		 */
		if ((func->gne_flags & GENOA_NBIF_F_NO_CONFIG) != 0) {
			continue;
		}

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP0);
		strap = genoa_nbif_func_read(func, strapreg);

		if ((func->gne_flags & GENOA_NBIF_F_ENABLED) != 0) {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 1);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->gne_dev, func->gne_func, 1);

			/*
			 * Strap enabled SATA devices to what AMD asks for.
			 */
			if (func->gne_type == GENOA_NBIF_T_SATA) {
				strap = NBIF_FUNC_STRAP0_SET_MAJ_REV(strap, 7);
				strap = NBIF_FUNC_STRAP0_SET_MIN_REV(strap, 1);
			}
		} else {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 0);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->gne_dev, func->gne_func, 0);
		}

		genoa_nbif_func_write(func, strapreg, strap);
	}

	genoa_nbif_write(nbif, reg, intr);

	/*
	 * Each nBIF has up to three devices on them, though not all of them
	 * seem to be used. However, it's suggested that we enable completion
	 * timeouts on all three device straps.
	 */
	for (uint8_t devno = 0; devno < GENOA_NBIF_MAX_DEVS; devno++) {
		smn_reg_t reg;
		uint32_t val;

		reg = genoa_nbif_reg(nbif, D_NBIF_PORT_STRAP3, devno);
		val = genoa_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP3_SET_COMP_TO(val, 1);
		genoa_nbif_write(nbif, reg, val);
	}

	return (0);
}

/*
 * There are five bridges that are associated with the NBIFs. One on NBIF0,
 * three on NBIF1, and the last on the SB. There is nothing on NBIF 2 which is
 * why we don't use the nbif iterator, though this is somewhat uglier. The
 * default expectation of the system is that the CRS bit is set. XXX these have
 * all been left enabled for now.
 */
static int
genoa_fabric_init_nbif_bridge(genoa_ioms_t *ioms, void *arg)
{
	uint32_t val;
	const smn_reg_t smn_regs[5] = {
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->gio_num, 0, 0),
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->gio_num, 1, 0),
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->gio_num, 1, 1),
		IOHCDEV_NBIF_BRIDGE_CTL(ioms->gio_num, 1, 2),
		IOHCDEV_SB_BRIDGE_CTL(ioms->gio_num)
	};

	for (uint_t i = 0; i < ARRAY_SIZE(smn_regs); i++) {
		val = genoa_ioms_read(ioms, smn_regs[i]);
		val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
		genoa_ioms_write(ioms, smn_regs[i], val);
	}
	return (0);
}

static int
genoa_dxio_init(genoa_iodie_t *iodie, void *arg)
{
	genoa_soc_t *soc = iodie->gi_soc;

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
	if (genoa_board_type(soc->gs_fabric) == MBT_RUBY) {
		if (soc->gs_socno == 0 && !genoa_dxio_rpc_sm_reload(iodie)) {
			return (1);
		}
	}


	if (!genoa_dxio_rpc_init(iodie)) {
		return (1);
	}

	/*
	 * XXX These 0x4f values were kind of given to us. Do better than a
	 * magic constant, rm.
	 */
	if (!genoa_dxio_rpc_clock_gating(iodie, 0x4f, 0x4f)) {
		return (1);
	}

	/*
	 * Set up a few different variables in firmware. Best guesses is that we
	 * need GENOA_DXIO_VAR_PCIE_COMPL so we can get PCIe completions to
	 * actually happen, GENOA_DXIO_VAR_SLIP_INTERVAL is disabled, but I
	 * can't say why. XXX We should probably disable NTB hotplug because we
	 * don't have them just in case something changes here.
	 */
	if (!genoa_dxio_rpc_set_var(iodie, GENOA_DXIO_VAR_PCIE_COMPL, 1) ||
	    !genoa_dxio_rpc_set_var(iodie, GENOA_DXIO_VAR_SLIP_INTERVAL, 0)) {
		return (1);
	}

	/*
	 * This seems to configure behavior when the link is going down and
	 * power off. We explicitly ask for no delay. The latter argument is
	 * about disabling another command (which we don't use), but to keep
	 * firmware in its expected path we don't set that.  Older DXIO firmware
	 * doesn't support this so we skip it there.
	 */
	if (genoa_dxio_version_at_least(iodie, 45, 682) &&
	    !genoa_dxio_rpc_pcie_poweroff_config(iodie, 0, false)) {
		return (1);
	}

	/*
	 * Next we set a couple of variables that are required for us to
	 * cause the state machine to pause after a couple of different stages
	 * and then also to indicate that we want to use the v1 ancillary data
	 * format.
	 */
	if (!genoa_dxio_rpc_set_var(iodie, MLIAN_DXIO_VAR_RET_AFTER_MAP, 1) ||
	    !genoa_dxio_rpc_set_var(iodie, GENOA_DXIO_VAR_RET_AFTER_CONF, 1) ||
	    !genoa_dxio_rpc_set_var(iodie, GENOA_DXIO_VAR_ANCILLARY_V1, 1)) {
		return (1);
	}

	/*
	 * Here, it's worth calling out what we're not setting. One of which is
	 * GENOA_DXIO_VAR_MAP_EXACT_MATCH which ends up being used to cause
	 * the mapping phase to only work if there are exact matches. I believe
	 * this means that if a device has more lanes then the configured port,
	 * it wouldn't link up, which generally speaking isn't something we want
	 * to do. Similarly, since there is no S3 support here, no need to
	 * change the save and restore mode with GENOA_DXIO_VAR_S3_MODE.
	 *
	 * From here, we do want to set GENOA_DXIO_VAR_SKIP_PSP, because the PSP
	 * really doesn't need to do anything with us. We do want to enable
	 * GENOA_DXIO_VAR_PHY_PROG so the dxio engine can properly configure
	 * things.
	 *
	 * XXX Should we gamble and set things that aren't unconditionally set
	 * so we don't rely on hw defaults?
	 */
	if (!genoa_dxio_rpc_set_var(iodie, GENOA_DXIO_VAR_PHY_PROG, 1) ||
	    !genoa_dxio_rpc_set_var(iodie, GENOA_DXIO_VAR_SKIP_PSP, 1)) {
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
genoa_dxio_plat_data(genoa_iodie_t *iodie, void *arg)
{
	ddi_dma_attr_t attr;
	size_t engn_size;
	pfn_t pfn;
	genoa_mpio_config_t *conf = &iodie->gi_dxio_conf;
	genoa_soc_t *soc = iodie->gi_soc;
	const zen_mpio_platform_t *source_data;
	zen_mpio_anc_data_t *anc;
	const genoa_apob_phyovr_t *phy_override;
	size_t phy_len;
	int err;

	/*
	 * XXX Figure out how to best not hardcode Ethanol. Realistically
	 * probably an SP boot property.
	 */
	if (genoa_board_type(soc->gs_fabric) == MBT_RUBY) {
		if (soc->gs_socno == 0) {
			source_data = &ruby_engine_s0;
		}
	}

	engn_size = sizeof (zen_mpio_platform_t) +
	    source_data->zmp_nengines * sizeof (zen_mpio_engine_t);
	VERIFY3U(engn_size, <=, MMU_PAGESIZE);
	conf->gmc_conf_len = engn_size;

	genoa_smu_dma_attr(&attr);
	conf->gmc_alloc_len = MMU_PAGESIZE;
	conf->gmc_conf = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->gmc_conf, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->gmc_conf);
	conf->gmc_pa = mmu_ptob((uint64_t)pfn);

	bcopy(source_data, conf->gmc_conf, engn_size);

	/*
	 * We need to account for an extra 8 bytes, surprisingly. It's a good
	 * thing we have a page. Note, dxio wants this in uint32_t units. We do
	 * that when we make the RPC call. Finally, we want to make sure that if
	 * we're in an incomplete word, that we account for that in the length.
	 */
	conf->gmc_conf_len += 8;
	conf->gmc_conf_len = P2ROUNDUP(conf->gmc_conf_len, 4);

	phy_override = genoa_apob_find(GENOA_APOB_GROUP_FABRIC,
	    GENOA_APOB_FABRIC_PHY_OVERRIDE, 0, &phy_len, &err);
	if (phy_override == NULL) {
		if (err == ENOENT) {
			return (0);
		}

		cmn_err(CE_WARN, "failed to find phy override table in APOB: "
		    "0x%x", err);
		return (1);
	}
	if (phy_len < offsetof(genoa_apob_phyovr_t, gap_data[0])) {
		cmn_err(CE_WARN, "APOB phy override table is too short "
		    "(actual size 0x%lx)", phy_len);
		return (1);
	}

	/*
	 * The actual length of phy data is in gap_datalen; it must be no larger
	 * than the maximum and must fit in the APOB entry.
	 */
	if (phy_override->gap_datalen > GENOA_APOB_PHY_OVERRIDE_MAX_LEN ||
	    phy_override->gap_datalen >
	    phy_len - offsetof(genoa_apob_phyovr_t, gap_data[0])) {
		cmn_err(CE_WARN, "APOB phy override table data doesn't fit "
		    "(datalen = 0x%x, entry len = 0x%lx)",
		    phy_override->gap_datalen, phy_len);
		return (1);
	}

	/*
	 * The headers for the ancillary heap and payload must be 4 bytes in
	 * size.
	 */
	CTASSERT(sizeof (zen_mpio_anc_data_t) == 4);

	conf->gmc_anc = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->gmc_anc, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->gmc_anc);
	conf->gmc_anc_pa = mmu_ptob((uint64_t)pfn);

	/*
	 * First we need to program the initial descriptor. Its type is one of
	 * the Heap types. Yes, this is different from the sub data payloads
	 * that we use. Yes, this is different from the way that the engine
	 * config data is laid out. Each entry has the amount of space they take
	 * up. Confusingly, it seems that the top entry does not include the
	 * space its header takes up. However, the subsequent payloads do.
	 */
	anc = conf->gmc_anc;
	anc->zmad_type = GENOA_DXIO_HEAP_ANCILLARY;
	anc->zmad_vers = DXIO_ANCILLARY_VERSION;
	anc->zmad_nu32s = (sizeof (zen_mpio_anc_data_t) +
	    phy_override->gap_datalen) >> 2;
	anc++;
	anc->zmad_type = ZEN_MPIO_ANCILLARY_T_PHY_CONFIG;
	anc->zmad_vers = DXIO_ANCILLARY_PAYLOAD_VERSION;
	anc->zmad_nu32s = (sizeof (zen_mpio_anc_data_t) +
	    phy_override->gap_datalen) >> 2;
	anc++;
	bcopy(phy_override->gap_data, anc, phy_override->gap_datalen);
	conf->gmc_anc_len = phy_override->gap_datalen +
	    2 * sizeof (zen_mpio_anc_data_t);

	return (0);
}

static int
genoa_dxio_load_data(genoa_iodie_t *iodie, void *arg)
{
	genoa_mpio_config_t *conf = &iodie->gi_dxio_conf;

	/*
	 * Begin by loading the NULL capabilities before we load any data heaps.
	 */
	if (!genoa_dxio_rpc_load_caps(iodie)) {
		return (1);
	}

	if (conf->gmc_anc != NULL && !genoa_dxio_rpc_load_data(iodie,
	    GENOA_DXIO_HEAP_ANCILLARY, conf->gmc_anc_pa, conf->gmc_anc_len,
	    0)) {
		return (1);
	}

	/*
	 * It seems that we're required to load both of these heaps with the
	 * mystery bit set to one. It's called that because we don't know what
	 * it does; however, these heaps are always loaded with no data, even
	 * though ancillary is skipped if there is none.
	 */
	if (!genoa_dxio_rpc_load_data(iodie, GENOA_DXIO_HEAP_MACPCS,
	    0, 0, 1) ||
	    !genoa_dxio_rpc_load_data(iodie, GENOA_DXIO_HEAP_GPIO, 0, 0, 1)) {
		return (1);
	}

	/*
	 * Load our real data!
	 */
	if (!genoa_dxio_rpc_load_data(iodie, GENOA_DXIO_HEAP_ENGINE_CONFIG,
	    conf->gmc_pa, conf->gmc_conf_len, 0)) {
		return (1);
	}

	return (0);
}

static int
genoa_dxio_more_conf(genoa_iodie_t *iodie, void *arg)
{
	/*
	 * Note, here we might use genoa_dxio_rpc_conf_training() if we want to
	 * override any of the properties there. But the defaults in DXIO
	 * firmware seem to be used by default. We also might apply various
	 * workarounds that we don't seem to need to
	 * (GENOA_DXIO_RT_SET_CONF_DXIO_WA, GENOA_DXIO_RT_SET_CONF_SPC_WA,
	 * GENOA_DXIO_RT_SET_CONF_FC_CRED_WA_DIS).
	 */

	/*
	 * XXX Do we care about any of the following:
	 *    o GENOA_DXIO_RT_SET_CONF_TX_CLOCK
	 *    o GENOA_DXIO_RT_SET_CONF_SRNS
	 *    o GENOA_DXIO_RT_SET_CONF_DLF_WA_DIS
	 *
	 * I wonder why we don't enable GENOA_DXIO_RT_SET_CONF_CE_SRAM_ECC in
	 * the old world.
	 */

	/*
	 * This is set to 1 by default because we want 'latency behaviour' not
	 * 'improved latency'.
	 */
	if (!genoa_dxio_rpc_misc_rt_conf(iodie,
	    GENOA_DXIO_RT_SET_CONF_TX_FIFO_MODE, 1)) {
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
genoa_dxio_map_engines(genoa_fabric_t *fabric, genoa_iodie_t *iodie)
{
	bool ret = true;
	zen_mpio_platform_t *plat = iodie->gi_dxio_conf.gmc_conf;

	for (uint_t i = 0; i < plat->zmp_nengines; i++) {
		zen_mpio_engine_t *en = &plat->zmp_engines[i];
		genoa_pcie_core_t *pc;
		genoa_pcie_port_t *port;
		uint8_t portno;

		if (en->zme_type != ZEN_MPIO_ENGINE_PCIE)
			continue;


		pc = genoa_fabric_find_pcie_core_by_lanes(iodie,
		    en->zme_start_lane, en->zme_end_lane);
		if (pc == NULL) {
			cmn_err(CE_WARN, "failed to map engine %u [%u, %u] to "
			    "a PCIe core", i, en->zme_start_lane,
			    en->zme_end_lane);
			ret = false;
			continue;
		}

		portno = en->zme_config.zmc_pcie.zmcp_mac_port_id;
		if (portno >= pc->gpc_nports) {
			cmn_err(CE_WARN, "failed to map engine %u [%u, %u] to "
			    "a PCIe port: found nports %u, but mapped to "
			    "port %u",  i, en->zme_start_lane,
			    en->zme_end_lane, pc->gpc_nports, portno);
			ret = false;
			continue;
		}

		port = &pc->gpc_ports[portno];
		if (port->gpp_engine != NULL) {
			cmn_err(CE_WARN, "engine %u [%u, %u] mapped to "
			    "port %u, which already has an engine [%u, %u]",
			    i, en->zme_start_lane, en->zme_end_lane,
			    pc->gpc_nports,
			    port->gpp_engine->zme_start_lane,
			    port->gpp_engine->zme_end_lane);
			ret = false;
			continue;
		}

		port->gpp_flags |= GENOA_PCIE_PORT_F_MAPPED;
		port->gpp_engine = en;
		pc->gpc_flags |= GENOA_PCIE_CORE_F_USED;
		if (en->zme_config.zmc_pcie.zmcp_caps.zmlc_hotplug !=
		    ZEN_MPIO_HOTPLUG_T_DISABLED) {
			pc->gpc_flags |= GENOA_PCIE_CORE_F_HAS_HOTPLUG;
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
 * number, PCIe core number (pcie_core_t.gpc_coreno), and PCIe port number
 * (pcie_port_t.gpp_portno).  The board sentinel value MBT_ANY is 0 and may be
 * omitted, but the others require nonzero sentinels as 0 is a valid index.  The
 * sentinel values of 0xFF here cannot match any real NBIO, RC, or port: there
 * are at most 4 NBIOs per die, 3 RC per NBIO, and 8 ports (bridges) per RC.
 * The RC and port filters are meaningful only if the corresponding strap exists
 * at the corresponding level.  The node ID, which incorporates both socket and
 * die number (die number is always 0 for Genoa), is 8 bits so in principle it
 * could be 0xFF and we use 32 bits there instead.  While it's still 8 bits in
 * Genoa, AMD have reserved another 8 bits that are likely to be used in future
 * families so we opt to go all the way to 32 here.  This can be reevaluated
 * when this is refactored to support multiple families.
 */

#define	PCIE_NODEMATCH_ANY	0xFFFFFFFF
#define	PCIE_NBIOMATCH_ANY	0xFF
#define	PCIE_COREMATCH_ANY	0xFF
#define	PCIE_PORTMATCH_ANY	0xFF

typedef struct genoa_pcie_strap_setting {
	uint32_t		strap_reg;
	uint32_t		strap_data;
	genoa_board_type_t	strap_boardmatch;
	uint32_t		strap_nodematch;
	uint8_t			strap_nbiomatch;
	uint8_t			strap_corematch;
	uint8_t			strap_portmatch;
} genoa_pcie_strap_setting_t;

/*
 * PCIe Straps that we unconditionally set to 1
 */
static const uint32_t genoa_pcie_strap_enable[] = {
	GENOA_STRAP_PCIE_MSI_EN,
	GENOA_STRAP_PCIE_AER_EN,
	GENOA_STRAP_PCIE_GEN2_FEAT_EN,
	/* We want completion timeouts */
	GENOA_STRAP_PCIE_CPL_TO_EN,
	GENOA_STRAP_PCIE_TPH_EN,
	GENOA_STRAP_PCIE_MULTI_FUNC_EN,
	GENOA_STRAP_PCIE_DPC_EN,
	GENOA_STRAP_PCIE_ARI_EN,
	GENOA_STRAP_PCIE_PL_16G_EN,
	GENOA_STRAP_PCIE_LANE_MARGIN_EN,
	GENOA_STRAP_PCIE_LTR_SUP,
	GENOA_STRAP_PCIE_LINK_BW_NOTIF_SUP,
	GENOA_STRAP_PCIE_GEN3_1_FEAT_EN,
	GENOA_STRAP_PCIE_GEN4_FEAT_EN,
	GENOA_STRAP_PCIE_ECRC_GEN_EN,
	GENOA_STRAP_PCIE_ECRC_CHECK_EN,
	GENOA_STRAP_PCIE_CPL_ABORT_ERR_EN,
	GENOA_STRAP_PCIE_INT_ERR_EN,
	GENOA_STRAP_PCIE_RXP_ACC_FULL_DIS,

	/* ACS straps */
	GENOA_STRAP_PCIE_ACS_EN,
	GENOA_STRAP_PCIE_ACS_SRC_VALID,
	GENOA_STRAP_PCIE_ACS_TRANS_BLOCK,
	GENOA_STRAP_PCIE_ACS_DIRECT_TRANS_P2P,
	GENOA_STRAP_PCIE_ACS_P2P_CPL_REDIR,
	GENOA_STRAP_PCIE_ACS_P2P_REQ_RDIR,
	GENOA_STRAP_PCIE_ACS_UPSTREAM_FWD,
};

/*
 * PCIe Straps that we unconditionally set to 0
 * These are generally debug and test settings that are usually not a good idea
 * in my experience to allow accidental enablement.
 */
static const uint32_t genoa_pcie_strap_disable[] = {
	GENOA_STRAP_PCIE_I2C_DBG_EN,
	GENOA_STRAP_PCIE_DEBUG_RXP,
	GENOA_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST,
	GENOA_STRAP_PCIE_ERR_REPORT_DIS,
	GENOA_STRAP_PCIE_TX_TEST_ALL,
	GENOA_STRAP_PCIE_MCAST_EN,
};

/*
 * PCIe Straps that have other values.
 */
static const genoa_pcie_strap_setting_t genoa_pcie_strap_settings[] = {
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_DS_RX_PRESET_HINT,
		.strap_data = GENOA_STRAP_PCIE_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_US_RX_PRESET_HINT,
		.strap_data = GENOA_STRAP_PCIE_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_DS_TX_PRESET,
		.strap_data = GENOA_STRAP_PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_US_TX_PRESET,
		.strap_data = GENOA_STRAP_PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_16GT_EQ_DS_TX_PRESET,
		.strap_data = GENOA_STRAP_PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_16GT_EQ_US_TX_PRESET,
		.strap_data = GENOA_STRAP_PCIE_TX_PRESET_5,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_SUBVID,
		.strap_data = PCI_VENDOR_ID_OXIDE,
		.strap_boardmatch = MBT_COSMO,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_SUBDID,
		.strap_data = PCI_SDID_OXIDE_GIMLET_BASE,
		.strap_boardmatch = MBT_COSMO,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
};

/*
 * PCIe Straps that exist on a per-port level.  Most pertain to the port itself;
 * others pertain to features exposed via the associated bridge.
 */
static const genoa_pcie_strap_setting_t genoa_pcie_port_settings[] = {
	{
		.strap_reg = GENOA_STRAP_PCIE_P_EXT_FMT_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_E2E_TLP_PREFIX_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_10B_TAG_CMPL_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_10B_TAG_REQ_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TCOMMONMODE_TIME,
		.strap_data = 0xa,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TPON_SCALE,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TPON_VALUE,
		.strap_data = 0xf,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_DLF_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_DLF_EXCHANGE_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_FOM_TIME,
		.strap_data = GENOA_STRAP_PCIE_P_FOM_300US,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SPC_MODE_8GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SRIS_EN,
		.strap_data = 1,
		.strap_boardmatch = MBT_COSMO,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP,
		.strap_data = 0,
		.strap_boardmatch = MBT_COSMO,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP,
		.strap_data = 0,
		.strap_boardmatch = MBT_COSMO,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_L0s_EXIT_LAT,
		.strap_data = PCIE_LINKCAP_L0S_EXIT_LAT_MAX >> 12,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	}
};

static bool
genoa_pcie_strap_matches(const genoa_pcie_core_t *pc, uint8_t portno,
    const genoa_pcie_strap_setting_t *strap)
{
	const genoa_ioms_t *ioms = pc->gpc_ioms;
	const genoa_iodie_t *iodie = ioms->gio_iodie;
	const genoa_fabric_t *fabric = iodie->gi_soc->gs_fabric;
	const genoa_board_type_t board = genoa_board_type(fabric);

	if (strap->strap_boardmatch != MBT_ANY &&
	    strap->strap_boardmatch != board) {
		return (false);
	}

	if (strap->strap_nodematch != PCIE_NODEMATCH_ANY &&
	    strap->strap_nodematch != (uint32_t)iodie->gi_node_id) {
		return (false);
	}

	if (strap->strap_nbiomatch != PCIE_NBIOMATCH_ANY &&
	    strap->strap_nbiomatch != ioms->gio_num) {
		return (false);
	}

	if (strap->strap_corematch != PCIE_COREMATCH_ANY &&
	    strap->strap_corematch != pc->gpc_coreno) {
		return (false);
	}

	if (portno != PCIE_PORTMATCH_ANY &&
	    strap->strap_portmatch != PCIE_PORTMATCH_ANY &&
	    strap->strap_portmatch != portno) {
		return (false);
	}

	return (true);
}

static void
genoa_fabric_write_pcie_strap(genoa_pcie_core_t *pc,
    const uint32_t reg, const uint32_t data)
{
	smn_reg_t a_reg, d_reg;

	a_reg = genoa_pcie_core_reg(pc, D_PCIE_RSMU_STRAP_ADDR);
	d_reg = genoa_pcie_core_reg(pc, D_PCIE_RSMU_STRAP_DATA);

	mutex_enter(&pc->gpc_strap_lock);
	genoa_pcie_core_write(pc, a_reg, GENOA_STRAP_PCIE_ADDR_UPPER + reg);
	genoa_pcie_core_write(pc, d_reg, data);
	mutex_exit(&pc->gpc_strap_lock);
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
genoa_fabric_init_pcie_straps(genoa_pcie_core_t *pc, void *arg)
{
	const genoa_iodie_t *iodie = (const genoa_iodie_t *)arg;

	if (iodie != NULL && pc->gpc_ioms->gio_iodie != iodie)
		return (0);

	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_enable); i++) {
		genoa_fabric_write_pcie_strap(pc,
		    genoa_pcie_strap_enable[i], 0x1);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_disable); i++) {
		genoa_fabric_write_pcie_strap(pc,
		    genoa_pcie_strap_disable[i], 0x0);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_settings); i++) {
		const genoa_pcie_strap_setting_t *strap =
		    &genoa_pcie_strap_settings[i];

		if (genoa_pcie_strap_matches(pc, PCIE_PORTMATCH_ANY, strap)) {
			genoa_fabric_write_pcie_strap(pc,
			    strap->strap_reg, strap->strap_data);
		}
	}

	/* Handle Special case for DLF which needs to be set on non WAFL */
	if (pc->gpc_coreno != GENOA_IOMS_WAFL_PCIE_CORENO) {
		genoa_fabric_write_pcie_strap(pc, GENOA_STRAP_PCIE_DLF_EN, 1);
	}

	/* Handle per bridge initialization */
	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_port_settings); i++) {
		const genoa_pcie_strap_setting_t *strap =
		    &genoa_pcie_port_settings[i];
		for (uint_t j = 0; j < pc->gpc_nports; j++) {
			if (genoa_pcie_strap_matches(pc, j, strap)) {
				genoa_fabric_write_pcie_strap(pc,
				    strap->strap_reg +
				    (j * GENOA_STRAP_PCIE_NUM_PER_PORT),
				    strap->strap_data);
			}
		}
	}

	return (0);
}

static int
genoa_fabric_setup_pcie_core_dbg(genoa_pcie_core_t *pc, void *arg)
{
	for (uint16_t portno = 0; portno < pc->gpc_nports; portno++) {
		genoa_pcie_port_t *port = &pc->gpc_ports[portno];

		if (port->gpp_flags & GENOA_PCIE_PORT_F_MAPPED) {
			smn_reg_t reg;
			uint32_t val;
			uint8_t laneno;

			/*
			 * This is the first mapped port in this core.  Enable
			 * core-level debugging capture for this port, and only
			 * this port.
			 */
			reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_DBG_CTL);
			val = genoa_pcie_core_read(pc, reg);
			val = PCIE_CORE_DBG_CTL_SET_PORT_EN(val, 1U << portno);
			genoa_pcie_core_write(pc, reg, val);

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
			laneno = port->gpp_engine->zme_start_lane -
			    pc->gpc_dxio_lane_start;
			reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_LC_DBG_CTL);
			val = genoa_pcie_core_read(pc, reg);
			val = PCIE_CORE_LC_DBG_CTL_SET_LANE_MASK(val,
			    1U << laneno);
			genoa_pcie_core_write(pc, reg, val);

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
genoa_dxio_state_machine(genoa_iodie_t *iodie, void *arg)
{
	genoa_soc_t *soc = iodie->gi_soc;
	genoa_fabric_t *fabric = soc->gs_fabric;

	if (!genoa_dxio_rpc_sm_start(iodie)) {
		return (1);
	}

	for (;;) {
		genoa_dxio_reply_t reply = { 0 };

		if (!genoa_dxio_rpc_sm_getstate(iodie, &reply)) {
			return (1);
		}

		switch (reply.gdr_type) {
		case GENOA_DXIO_DATA_TYPE_SM:
			cmn_err(CE_CONT, "?Socket %u LISM 0x%x->0x%x\n",
			    soc->gs_socno, iodie->gi_state, reply.gdr_arg0);
			iodie->gi_state = reply.gdr_arg0;
			switch (iodie->gi_state) {
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
			case GENOA_DXIO_SM_MAPPED:
				genoa_pcie_populate_dbg(&genoa_fabric,
				    GPCS_DXIO_SM_MAPPED, iodie->gi_node_id);

				if (!genoa_dxio_rpc_retrieve_engine(iodie)) {
					return (1);
				}

				if (!genoa_dxio_map_engines(fabric, iodie)) {
					cmn_err(CE_WARN, "Socket %u LISM: "
					    "failed to map all DXIO engines to "
					    "devices.  PCIe will not function",
					    soc->gs_socno);
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
				(void) genoa_fabric_walk_pcie_core(fabric,
				    genoa_fabric_init_pcie_straps, iodie);
				cmn_err(CE_CONT, "?Socket %u LISM: Finished "
				    "writing PCIe straps\n", soc->gs_socno);

				/*
				 * Set up the core-level debugging controls so
				 * that we get extended data for the first port
				 * in the core that's been mapped.
				 */
				(void) genoa_fabric_walk_pcie_core(fabric,
				    genoa_fabric_setup_pcie_core_dbg, NULL);

				genoa_pcie_populate_dbg(&genoa_fabric,
				    GPCS_DXIO_SM_MAPPED_RESUME,
				    iodie->gi_node_id);
				break;
			case GENOA_DXIO_SM_CONFIGURED:
				genoa_pcie_populate_dbg(&genoa_fabric,
				    GPCS_DXIO_SM_CONFIGURED, iodie->gi_node_id);

				/*
				 * XXX There is a substantial body of additional
				 * things that can be done here; investigation
				 * is needed.
				 */

				genoa_pcie_populate_dbg(&genoa_fabric,
				    GPCS_DXIO_SM_CONFIGURED_RESUME,
				    iodie->gi_node_id);
				break;
			case GENOA_DXIO_SM_DONE:
				/*
				 * We made it. Somehow we're done!
				 */
				cmn_err(CE_CONT, "?Socket %u LISM: done\n",
				    soc->gs_socno);
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
		case GENOA_DXIO_DATA_TYPE_RESET:
			genoa_pcie_populate_dbg(&genoa_fabric,
			    GPCS_DXIO_SM_PERST, iodie->gi_node_id);
			cmn_err(CE_CONT, "?Socket %u LISM: PERST %x, %x\n",
			    soc->gs_socno, reply.gdr_arg0, reply.gdr_arg1);
			if (reply.gdr_arg0 == 0) {
				cmn_err(CE_NOTE, "Socket %u LISM: disregarding "
				    "request to assert PERST at index 0x%x",
				    soc->gs_socno, reply.gdr_arg1);
				break;
			}

			if (genoa_board_type(fabric) == MBT_RUBY) {

				/*
				 * Release PERST manually on Ruby which
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
				 * invoke GHGOP_CONFIGURE first.  This also
				 * works only for socket 0; we can't access the
				 * FCH on socket 1 because won't let us use SMN
				 * and we haven't set up the secondary FCH
				 * aperture here.  This most likely means the
				 * NVMe sockets won't work.
				 */
				if (iodie->gi_node_id == 0) {
					genoa_hack_gpio(GHGOP_SET, 26);
					genoa_hack_gpio(GHGOP_SET, 27);
					genoa_hack_gpio(GHGOP_SET, 266);
					genoa_hack_gpio(GHGOP_SET, 267);
				}
			}

			genoa_pcie_populate_dbg(&genoa_fabric,
			    GPCS_DXIO_SM_PERST_RESUME, iodie->gi_node_id);

			break;
		case GENOA_DXIO_DATA_TYPE_NONE:
			cmn_err(CE_WARN, "Socket %u LISM: Got the none data "
			    "type... are we actually done?", soc->gs_socno);
			goto done;
		default:
			cmn_err(CE_WARN, "Socket %u LISM: Got unexpected DXIO "
			    "return type 0x%x. PCIe will not function.",
			    soc->gs_socno, reply.gdr_type);
			return (1);
		}

		if (!genoa_dxio_rpc_sm_resume(iodie)) {
			return (1);
		}
	}

done:
	genoa_pcie_populate_dbg(&genoa_fabric, GPCS_DXIO_SM_DONE,
	    iodie->gi_node_id);

	if (!genoa_dxio_rpc_retrieve_engine(iodie)) {
		return (1);
	}

	return (0);
}

/*
 * Our purpose here is to set up memlist structures for use in tracking. Right
 * now we use the xmemlist feature, though having something that is backed by
 * kmem would make life easier; however, that will wait for the great memlist
 * merge that is likely not to happen anytime soon.
 */
static int
genoa_fabric_init_memlists(genoa_ioms_t *ioms, void *arg)
{
	ioms_memlists_t *imp = &ioms->gio_memlists;
	void *page = kmem_zalloc(MMU_PAGESIZE, KM_SLEEP);

	mutex_init(&imp->im_lock, NULL, MUTEX_DRIVER, NULL);
	xmemlist_free_block(&imp->im_pool, page, MMU_PAGESIZE);
	return (0);
}

/*
 * We want to walk the DF and record information about how PCI buses are routed.
 * We make an assumption here, which is that each DF instance has been
 * programmed the same way by the PSP/SMU (which if was not done would lead to
 * some chaos). As such, we end up using the first socket's df and its first
 * IOM to figure this out.
 */
static void
genoa_route_pci_bus(genoa_fabric_t *fabric)
{
	genoa_iodie_t *iodie = &fabric->gf_socs[0].gs_iodies[0];
	uint_t inst = iodie->gi_ioms[0].gio_iom_inst_id;

	for (uint_t i = 0; i < DF_MAX_CFGMAP; i++) {
		int ret;
		genoa_ioms_t *ioms;
		ioms_memlists_t *imp;
		uint32_t base, limit, dest;
		uint32_t val = genoa_df_read32(iodie, inst,
		    DF_CFGMAP_BASE_V4(i));

		/*
		 * If a configuration map entry doesn't have both read and write
		 * enabled, then we treat that as something that we should skip.
		 * There is no validity bit here, so this is the closest that we
		 * can come to.
		 */
		if (DF_CFGMAP_BASE_V4_GET_RE(val) == 0 ||
		    DF_CFGMAP_BASE_V4_GET_WE(val) == 0) {
			continue;
		}

		base = DF_CFGMAP_BASE_V4_GET_BASE(val);

		val = genoa_df_read32(iodie, inst, DF_CFGMAP_LIMIT_V4(i));
		limit = DF_CFGMAP_LIMIT_V4_GET_LIMIT(val);
		dest = DF_CFGMAP_LIMIT_V4_GET_DEST_ID(val);

		ioms = genoa_fabric_find_ioms(fabric, dest);
		if (ioms == NULL) {
			cmn_err(CE_WARN, "PCI Bus fabric rule %u [0x%x, 0x%x] "
			    "maps to unknown fabric id: 0x%x", i, base, limit,
			    dest);
			continue;
		}
		imp = &ioms->gio_memlists;

		if (base != ioms->gio_pci_busno) {
			cmn_err(CE_PANIC, "unexpected bus routing rule, rule "
			    "base 0x%x does not match destination base: 0x%x",
			    base, ioms->gio_pci_busno);
		}

		/*
		 * We assign the IOMS's PCI bus as used and all the remainin as
		 * available.
		 */
		ret = xmemlist_add_span(&imp->im_pool, base, 1,
		    &imp->im_bus_used, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		if (base == limit)
			continue;
		ret = xmemlist_add_span(&imp->im_pool, base + 1, limit - base,
		    &imp->im_bus_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}
}

#define	GENOA_SEC_IOMS_GEN_IO_SPACE	0x1000

typedef struct genoa_route_io {
	uint32_t	mri_per_ioms;
	uint32_t	mri_next_base;
	uint32_t	mri_cur;
	uint32_t	mri_last_ioms;
	uint32_t	mri_bases[DF_MAX_IO_RULES];
	uint32_t	mri_limits[DF_MAX_IO_RULES];
	uint32_t	mri_dests[DF_MAX_IO_RULES];
} genoa_route_io_t;

static int
genoa_io_ports_allocate(genoa_ioms_t *ioms, void *arg)
{
	int ret;
	genoa_route_io_t *mri = arg;
	ioms_memlists_t *imp = &ioms->gio_memlists;
	uint32_t pci_base;

	/*
	 * The primary FCH (e.g. the IOMS that has the FCH on iodie 0) always
	 * has a base of zero so we can cover the legacy I/O ports.  That range
	 * is not available for PCI allocation, however.
	 */
	if ((ioms->gio_flags & GENOA_IOMS_F_HAS_FCH) != 0 &&
	    (ioms->gio_iodie->gi_flags & GENOA_IODIE_F_PRIMARY) != 0) {
		mri->mri_bases[mri->mri_cur] = 0;
		pci_base = GENOA_IOPORT_COMPAT_SIZE;
	} else if (mri->mri_per_ioms > 2 * GENOA_SEC_IOMS_GEN_IO_SPACE) {
		mri->mri_bases[mri->mri_cur] = mri->mri_next_base;
		pci_base = mri->mri_bases[mri->mri_cur] +
		    GENOA_SEC_IOMS_GEN_IO_SPACE;
		mri->mri_next_base += mri->mri_per_ioms;

		mri->mri_last_ioms = mri->mri_cur;
	} else {
		pci_base = mri->mri_bases[mri->mri_cur] = mri->mri_next_base;
		mri->mri_next_base += mri->mri_per_ioms;

		mri->mri_last_ioms = mri->mri_cur;
	}

	mri->mri_limits[mri->mri_cur] = mri->mri_bases[mri->mri_cur] +
	    mri->mri_per_ioms - 1;
	mri->mri_dests[mri->mri_cur] = ioms->gio_ios_fabric_id;

	/*
	 * We must always have some I/O port space available for PCI.  The PCI
	 * space must always be higher than any space reserved for generic/FCH
	 * use.  While this is ultimately due to the way the hardware works, the
	 * more important reason is that our memlist code below relies on it.
	 */
	ASSERT3U(mri->mri_limits[mri->mri_cur], >, pci_base);
	ASSERT3U(mri->mri_bases[mri->mri_cur], <=, pci_base);

	/*
	 * We purposefully assign all of the I/O ports here and not later on as
	 * we want to make sure that we don't end up recording the fact that
	 * someone has the rest of the ports that aren't available on x86.
	 * While there is some logic in pci_boot.c that attempts to avoid
	 * allocating the legacy/compatibility space port range to PCI
	 * endpoints, it's better to tell that code exactly what's really
	 * available and what isn't.  We also need to reserve the compatibility
	 * space for later allocation to FCH devices if the FCH driver or one of
	 * its children requests it.
	 */
	if (pci_base != mri->mri_bases[mri->mri_cur]) {
		ret = xmemlist_add_span(&imp->im_pool,
		    mri->mri_bases[mri->mri_cur], pci_base,
		    &imp->im_io_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}
	ret = xmemlist_add_span(&imp->im_pool, pci_base,
	    mri->mri_limits[mri->mri_cur] - mri->mri_bases[mri->mri_cur] + 1,
	    &imp->im_io_avail_pci, 0);
	VERIFY3S(ret, ==, MEML_SPANOP_OK);

	mri->mri_cur++;
	return (0);
}

/*
 * The I/O ports effectively use the RE and WE bits as enable bits. Therefore we
 * need to make sure to set the limit register before setting the base register
 * for a given entry.
 */
static int
genoa_io_ports_assign(genoa_iodie_t *iodie, void *arg)
{
	genoa_route_io_t *mri = arg;

	for (uint32_t i = 0; i < mri->mri_cur; i++) {
		uint32_t base = 0, limit = 0;

		base = DF_IO_BASE_V4_SET_RE(base, 1);
		base = DF_IO_BASE_V4_SET_WE(base, 1);
		base = DF_IO_BASE_V4_SET_BASE(base,
		    mri->mri_bases[i] >> DF_IO_BASE_SHIFT);

		limit = DF_IO_LIMIT_V4_SET_DEST_ID(limit, mri->mri_dests[i]);
		limit = DF_IO_LIMIT_V4_SET_LIMIT(limit,
		    mri->mri_limits[i] >> DF_IO_LIMIT_SHIFT);

		genoa_df_bcast_write32(iodie, DF_IO_LIMIT_V4(i), limit);
		genoa_df_bcast_write32(iodie, DF_IO_BASE_V4(i), base);
	}

	return (0);
}

/*
 * We need to set up the I/O port mappings to all IOMS instances. Like with
 * other things, for the moment we do the simple thing and make them shared
 * equally across all units. However, there are a few gotchas:
 *
 *  o The first 4 KiB of I/O ports are considered 'legacy'/'compatibility' I/O.
 *    This means that they need to go to the IOMS with the FCH.
 *  o The I/O space base and limit registers all have a 12-bit granularity.
 *  o The DF actually supports 24-bits of I/O space
 *  o x86 cores only support 16-bits of I/O space
 *  o There are only 8 routing rules here, so 1/IOMS in a 2P system
 *
 * So with all this in mind, we're going to do the following:
 *
 *  o Each IOMS will be assigned a single route (whether there are 4 or 8)
 *  o We're basically going to assign the 16-bits of ports evenly between all
 *    found IOMS instances.
 *  o Yes, this means the FCH is going to lose some I/O ports relative to
 *    everything else, but that's fine. If we're constrained on I/O ports, we're
 *    in trouble.
 *  o Because we have a limited number of entries, the FCH on node 0 (e.g. the
 *    primary one) has the region starting at 0.
 *  o Whoever is last gets all the extra I/O ports filling up the 1 MiB.
 */
static void
genoa_route_io_ports(genoa_fabric_t *fabric)
{
	genoa_route_io_t mri;
	uint32_t total_size = UINT16_MAX + 1;

	bzero(&mri, sizeof (mri));
	mri.mri_per_ioms = total_size / fabric->gf_total_ioms;
	VERIFY3U(mri.mri_per_ioms, >=, 1 << DF_IO_BASE_SHIFT);
	mri.mri_next_base = mri.mri_per_ioms;

	/*
	 * First walk each IOMS to assign things evenly. We'll come back and
	 * then find the last non-primary one and that'll be the one that gets a
	 * larger limit.
	 */
	(void) genoa_fabric_walk_ioms(fabric, genoa_io_ports_allocate, &mri);
	mri.mri_limits[mri.mri_last_ioms] = DF_MAX_IO_LIMIT;
	(void) genoa_fabric_walk_iodie(fabric, genoa_io_ports_assign, &mri);
}

#define	GENOA_SEC_IOMS_GEN_MMIO32_SPACE 0x10000
#define	GENOA_SEC_IOMS_GEN_MMIO64_SPACE 0x10000

typedef struct genoa_route_mmio {
	uint32_t	mrm_cur;
	uint32_t	mrm_mmio32_base;
	uint32_t	mrm_mmio32_chunks;
	uint32_t	mrm_fch_base;
	uint32_t	mrm_fch_chunks;
	uint64_t	mrm_mmio64_base;
	uint64_t	mrm_mmio64_chunks;
	uint64_t	mrm_bases[DF_MAX_MMIO_RULES];
	uint64_t	mrm_limits[DF_MAX_MMIO_RULES];
	uint32_t	mrm_dests[DF_MAX_MMIO_RULES];
} genoa_route_mmio_t;

/*
 * We allocate two rules per device. The first is a 32-bit rule. The second is
 * then its corresponding 64-bit.  32-bit memory is always treated as
 * non-prefetchable due to the dearth of it.  64-bit memory is only treated as
 * prefetchable because we can't practically do anything else with it due to
 * the limitations of PCI-PCI bridges (64-bit memory has to be prefetch).
 */
static int
genoa_mmio_allocate(genoa_ioms_t *ioms, void *arg)
{
	int ret;
	genoa_route_mmio_t *mrm = arg;
	const uint32_t mmio_gran = 1 << DF_MMIO_SHIFT;
	ioms_memlists_t *imp = &ioms->gio_memlists;
	uint32_t gen_base32 = 0;
	uint64_t gen_base64 = 0;

	/*
	 * The primary FCH is treated as a special case so that its 32-bit MMIO
	 * region is as close to the subtractive compat region as possible.
	 * That region must not be made available for PCI allocation, but we do
	 * need to keep track of where it is so the FCH driver or its children
	 * can allocate from it.
	 */
	if ((ioms->gio_flags & GENOA_IOMS_F_HAS_FCH) != 0 &&
	    (ioms->gio_iodie->gi_flags & GENOA_IODIE_F_PRIMARY) != 0) {
		mrm->mrm_bases[mrm->mrm_cur] = mrm->mrm_fch_base;
		mrm->mrm_limits[mrm->mrm_cur] = mrm->mrm_fch_base;
		mrm->mrm_limits[mrm->mrm_cur] += mrm->mrm_fch_chunks *
		    mmio_gran - 1;
		ret = xmemlist_add_span(&imp->im_pool,
		    mrm->mrm_limits[mrm->mrm_cur] + 1, GENOA_COMPAT_MMIO_SIZE,
		    &imp->im_mmio_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	} else {
		mrm->mrm_bases[mrm->mrm_cur] = mrm->mrm_mmio32_base;
		mrm->mrm_limits[mrm->mrm_cur] = mrm->mrm_mmio32_base;
		mrm->mrm_limits[mrm->mrm_cur] += mrm->mrm_mmio32_chunks *
		    mmio_gran - 1;
		mrm->mrm_mmio32_base += mrm->mrm_mmio32_chunks *
		    mmio_gran;

		if (mrm->mrm_mmio32_chunks * mmio_gran >
		    2 * GENOA_SEC_IOMS_GEN_MMIO32_SPACE) {
			gen_base32 = mrm->mrm_limits[mrm->mrm_cur] -
			    (GENOA_SEC_IOMS_GEN_MMIO32_SPACE - 1);
		}
	}

	/*
	 * For secondary FCHs (and potentially any other non-PCI destination) we
	 * reserve a small amount of space for general use and give the rest to
	 * PCI.  If there's not enough, we give it all to PCI.
	 */
	mrm->mrm_dests[mrm->mrm_cur] = ioms->gio_ios_fabric_id;
	if (gen_base32 != 0) {
		ret = xmemlist_add_span(&imp->im_pool,
		    mrm->mrm_bases[mrm->mrm_cur],
		    mrm->mrm_limits[mrm->mrm_cur] -
		    mrm->mrm_bases[mrm->mrm_cur] -
		    GENOA_SEC_IOMS_GEN_MMIO32_SPACE + 1,
		    &imp->im_mmio_avail_pci, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		ret = xmemlist_add_span(&imp->im_pool, gen_base32,
		    GENOA_SEC_IOMS_GEN_MMIO32_SPACE,
		    &imp->im_mmio_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	} else {
		ret = xmemlist_add_span(&imp->im_pool,
		    mrm->mrm_bases[mrm->mrm_cur],
		    mrm->mrm_limits[mrm->mrm_cur] -
		    mrm->mrm_bases[mrm->mrm_cur] + 1,
		    &imp->im_mmio_avail_pci, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}

	mrm->mrm_cur++;

	/*
	 * Now onto the 64-bit register, which is thankfully uniform for all
	 * IOMS entries.
	 */
	mrm->mrm_bases[mrm->mrm_cur] = mrm->mrm_mmio64_base;
	mrm->mrm_limits[mrm->mrm_cur] = mrm->mrm_mmio64_base +
	    mrm->mrm_mmio64_chunks * mmio_gran - 1;
	mrm->mrm_mmio64_base += mrm->mrm_mmio64_chunks * mmio_gran;
	mrm->mrm_dests[mrm->mrm_cur] = ioms->gio_ios_fabric_id;

	if (mrm->mrm_mmio64_chunks * mmio_gran >
	    2 * GENOA_SEC_IOMS_GEN_MMIO64_SPACE) {
		gen_base64 = mrm->mrm_limits[mrm->mrm_cur] -
		    (GENOA_SEC_IOMS_GEN_MMIO64_SPACE - 1);

		ret = xmemlist_add_span(&imp->im_pool,
		    mrm->mrm_bases[mrm->mrm_cur],
		    mrm->mrm_limits[mrm->mrm_cur] -
		    mrm->mrm_bases[mrm->mrm_cur] -
		    GENOA_SEC_IOMS_GEN_MMIO64_SPACE + 1,
		    &imp->im_pmem_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		ret = xmemlist_add_span(&imp->im_pool, gen_base64,
		    GENOA_SEC_IOMS_GEN_MMIO64_SPACE,
		    &imp->im_mmio_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	} else {
		ret = xmemlist_add_span(&imp->im_pool,
		    mrm->mrm_bases[mrm->mrm_cur],
		    mrm->mrm_limits[mrm->mrm_cur] -
		    mrm->mrm_bases[mrm->mrm_cur] + 1,
		    &imp->im_pmem_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}

	mrm->mrm_cur++;

	return (0);
}

/*
 * We need to set the three registers that make up an MMIO rule. Importantly we
 * set the control register last as that's what contains the effective enable
 * bits.
 */
static int
genoa_mmio_assign(genoa_iodie_t *iodie, void *arg)
{
	genoa_route_mmio_t *mrm = arg;

	for (uint32_t i = 0; i < mrm->mrm_cur; i++) {
		uint32_t base, limit;
		uint32_t ctrl = 0;

		base = mrm->mrm_bases[i] >> DF_MMIO_SHIFT;
		limit = mrm->mrm_limits[i] >> DF_MMIO_SHIFT;
		ctrl = DF_MMIO_CTL_SET_RE(ctrl, 1);
		ctrl = DF_MMIO_CTL_SET_WE(ctrl, 1);
		ctrl = DF_MMIO_CTL_V4_SET_DEST_ID(ctrl, mrm->mrm_dests[i]);

		genoa_df_bcast_write32(iodie, DF_MMIO_BASE_V4(i), base);
		genoa_df_bcast_write32(iodie, DF_MMIO_LIMIT_V4(i), limit);
		genoa_df_bcast_write32(iodie, DF_MMIO_CTL_V4(i), ctrl);
	}

	return (0);
}

/*
 * Routing MMIO is both important and a little complicated mostly due to the how
 * x86 actually has historically split MMIO between the below 4 GiB region and
 * the above 4 GiB region. In addition, there are only 16 routing rules that we
 * can write, which means we get a maximum of 2 routing rules per IOMS (mostly
 * because we're being lazy).
 *
 * The below 4 GiB space is split due to the compat region
 * (GENOA_PHYSADDR_COMPAT_MMIO).  The way we divide up the lower region is
 * simple:
 *
 *   o The region between TOM and 4 GiB is split evenly among all IOMSs.
 *     In a 1P system with the MMIO base set at 0x8000_0000 (as it always is in
 *     the oxide architecture) this results in 512 MiB per IOMS; with 2P it's
 *     simply half that.
 *
 *   o The part of this region at the top is assigned to the IOMS with the FCH
 *     A small part of this is removed from this routed region to account for
 *     the adjacent FCH compatibility space immediately below 4 GiB; the
 *     remainder is routed to the primary root bridge.
 *
 * 64-bit space is also simple. We find which is higher: TOM2 or the top of the
 * second hole (GENOA_PHYSADDR_IOMMU_HOLE_END).  The 256 MiB ECAM region lives
 * there; above it, we just divide all the remaining space between that and
 * GENOA_PHYSADDR_MMIO_END. This is the genoa_fabric_t's gf_mmio64_base member.
 *
 * Our general assumption with this strategy is that 64-bit MMIO is plentiful
 * and that's what we'd rather assign and use.  This ties into the last bit
 * which is important: the hardware requires us to allocate in 16-bit chunks. So
 * we actually really treat all of our allocations as units of 64 KiB.
 */
static void
genoa_route_mmio(genoa_fabric_t *fabric)
{
	uint32_t mmio32_size;
	uint64_t mmio64_size;
	uint_t nioms32;
	genoa_route_mmio_t mrm;
	const uint32_t mmio_gran = DF_MMIO_LIMIT_EXCL;

	VERIFY(IS_P2ALIGNED(fabric->gf_tom, mmio_gran));
	VERIFY3U(GENOA_PHYSADDR_COMPAT_MMIO, >, fabric->gf_tom);
	mmio32_size = GENOA_PHYSADDR_MMIO32_END - fabric->gf_tom;
	nioms32 = fabric->gf_total_ioms;
	VERIFY3U(mmio32_size, >,
	    nioms32 * mmio_gran + GENOA_COMPAT_MMIO_SIZE);

	VERIFY(IS_P2ALIGNED(fabric->gf_mmio64_base, mmio_gran));
	VERIFY3U(GENOA_PHYSADDR_MMIO_END, >, fabric->gf_mmio64_base);
	mmio64_size = GENOA_PHYSADDR_MMIO_END - fabric->gf_mmio64_base;
	VERIFY3U(mmio64_size, >,  fabric->gf_total_ioms * mmio_gran);

	CTASSERT(IS_P2ALIGNED(GENOA_PHYSADDR_COMPAT_MMIO, DF_MMIO_LIMIT_EXCL));

	bzero(&mrm, sizeof (mrm));
	mrm.mrm_mmio32_base = fabric->gf_tom;
	mrm.mrm_mmio32_chunks = mmio32_size / mmio_gran / nioms32;
	mrm.mrm_fch_base = GENOA_PHYSADDR_MMIO32_END - mmio32_size / nioms32;
	mrm.mrm_fch_chunks = mrm.mrm_mmio32_chunks -
	    GENOA_COMPAT_MMIO_SIZE / mmio_gran;
	mrm.mrm_mmio64_base = fabric->gf_mmio64_base;
	mrm.mrm_mmio64_chunks = mmio64_size / mmio_gran / fabric->gf_total_ioms;

	(void) genoa_fabric_walk_ioms(fabric, genoa_mmio_allocate, &mrm);
	(void) genoa_fabric_walk_iodie(fabric, genoa_mmio_assign, &mrm);
}

static ioms_rsrc_t
genoa_ioms_prd_to_rsrc(pci_prd_rsrc_t rsrc)
{
	switch (rsrc) {
	case PCI_PRD_R_IO:
		return (IR_PCI_LEGACY);
	case PCI_PRD_R_MMIO:
		return (IR_PCI_MMIO);
	case PCI_PRD_R_PREFETCH:
		return (IR_PCI_PREFETCH);
	case PCI_PRD_R_BUS:
		return (IR_PCI_BUS);
	default:
		return (IR_NONE);
	}
}

static struct memlist *
genoa_fabric_rsrc_subsume(genoa_ioms_t *ioms, ioms_rsrc_t rsrc)
{
	ioms_memlists_t *imp;
	struct memlist **avail, **used, *ret;

	imp = &ioms->gio_memlists;
	mutex_enter(&imp->im_lock);
	switch (rsrc) {
	case IR_PCI_LEGACY:
		avail = &imp->im_io_avail_pci;
		used = &imp->im_io_used;
		break;
	case IR_PCI_MMIO:
		avail = &imp->im_mmio_avail_pci;
		used = &imp->im_mmio_used;
		break;
	case IR_PCI_PREFETCH:
		avail = &imp->im_pmem_avail;
		used = &imp->im_pmem_used;
		break;
	case IR_PCI_BUS:
		avail = &imp->im_bus_avail;
		used = &imp->im_bus_used;
		break;
	case IR_GEN_LEGACY:
		avail = &imp->im_io_avail_gen;
		used = &imp->im_io_used;
		break;
	case IR_GEN_MMIO:
		avail = &imp->im_mmio_avail_gen;
		used = &imp->im_mmio_used;
		break;
	default:
		mutex_exit(&imp->im_lock);
		return (NULL);
	}

	/*
	 * If there are no resources, that may be because there never were any
	 * or they had already been handed out.
	 */
	if (*avail == NULL) {
		mutex_exit(&imp->im_lock);
		return (NULL);
	}

	/*
	 * We have some resources available for this NB instance. In this
	 * particular case, we need to first duplicate these using kmem and then
	 * we can go ahead and move all of these to the used list.  This is done
	 * for the benefit of PCI code which expects it, but we do it
	 * universally for consistency.
	 */
	ret = memlist_kmem_dup(*avail, KM_SLEEP);

	/*
	 * XXX This ends up not really coalescing ranges, but maybe that's fine.
	 */
	while (*avail != NULL) {
		struct memlist *to_move = *avail;
		memlist_del(to_move, avail);
		memlist_insert(to_move, used);
	}

	mutex_exit(&imp->im_lock);
	return (ret);
}

/*
 * This is a request that we take resources from a given IOMS root port and
 * basically give what remains and hasn't been allocated to PCI. This is a bit
 * of a tricky process as we want to both:
 *
 *  1. Give everything that's currently available to PCI; however, it needs
 *     memlists that are allocated with kmem due to how PCI memlists work.
 *  2. We need to move everything that we're giving to PCI into our used list
 *     just for our own tracking purposes.
 */
struct memlist *
genoa_fabric_pci_subsume(uint32_t bus, pci_prd_rsrc_t rsrc)
{
	genoa_ioms_t *ioms;
	genoa_fabric_t *fabric = &genoa_fabric;
	ioms_rsrc_t ir;

	ioms = genoa_fabric_find_ioms_by_bus(fabric, bus);
	if (ioms == NULL) {
		return (NULL);
	}

	ir = genoa_ioms_prd_to_rsrc(rsrc);

	return (genoa_fabric_rsrc_subsume(ioms, ir));
}

/*
 * This is for the rest of the available legacy IO and MMIO space that we've set
 * aside for things that are not PCI.  The intent is that the caller will feed
 * the space to busra or the moral equivalent.  While this is presently used
 * only by the FCH and is set up only for the IOMSs that have an FCH attached,
 * in principle this could be applied to other users as well, including IOAPICs
 * and IOMMUs that are present in all NB instances.  For now this is really
 * about getting all this out of earlyboot context where we don't have modules
 * like rootnex and busra and into places where it's better managed; in this it
 * has the same purpose as its PCI counterpart above.  The memlists we supply
 * don't have to be allocated by kmem, but we do it anyway for consistency and
 * ease of use for callers.
 *
 * Curiously, AMD's documentation indicates that each of the PCI and non-PCI
 * regions associated with each NB instance must be contiguous, but there's no
 * hardware reason for that beyond the mechanics of assigning resources to PCIe
 * root ports.  So if we were to improve busra to manage these resources
 * globally instead of making PCI its own separate pool, we wouldn't need this
 * clumsy non-PCI reservation and could instead assign resources globally with
 * respect to each NB instance regardless of the requesting device type.  The
 * future's so bright, we gotta wear shades.
 */
struct memlist *
genoa_fabric_gen_subsume(genoa_ioms_t *ioms, ioms_rsrc_t ir)
{
	return (genoa_fabric_rsrc_subsume(ioms, ir));
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
genoa_fabric_init_bridges(genoa_pcie_port_t *port, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	bool hide;
	genoa_pcie_core_t *pc = port->gpp_core;
	genoa_ioms_t *ioms = pc->gpc_ioms;

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
	if ((port->gpp_flags & GENOA_PCIE_PORT_F_MAPPED) != 0) {
		bool hotplug, trained;
		uint8_t lt;

		hotplug = (pc->gpc_flags & GENOA_PCIE_CORE_F_HAS_HOTPLUG) != 0;
		lt = port->gpp_engine->zme_config.zmc_pcie.zmcp_link_train_state;
		trained = lt == GENOA_DXIO_PCIE_SUCCESS;
		hide = !hotplug && !trained;
	} else {
		hide = true;
	}

	if (hide) {
		port->gpp_flags |= GENOA_PCIE_PORT_F_BRIDGE_HIDDEN;
	}

	reg = genoa_pcie_port_reg(port, D_IOHCDEV_PCIE_BRIDGE_CTL);
	val = genoa_pcie_port_read(port, reg);
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
	genoa_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_TX_CTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_CTL_SET_TLP_FLUSH_DOWN_DIS(val, 0);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * Make sure the hardware knows the corresponding b/d/f for this bridge.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_TX_ID);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_ID_SET_BUS(val, ioms->gio_pci_busno);
	val = PCIE_PORT_TX_ID_SET_DEV(val, port->gpp_device);
	val = PCIE_PORT_TX_ID_SET_FUNC(val, port->gpp_func);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * Next, we have to go through and set up a bunch of the lane controller
	 * configuration controls for the individual port. These include
	 * various settings around how idle transitions occur, how it replies to
	 * certain messages, and related.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL_SET_L1_IMM_ACK(val, 1);
	genoa_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_L0S_L1_TRAIN(val, 1);
	genoa_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_WIDTH_CTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_DUAL_RECONFIG(val, 1);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_RENEG_EN(val, 1);
	genoa_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL2);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL2_SET_ELEC_IDLE(val,
	    PCIE_PORT_LC_CTL2_ELEC_IDLE_M1);
	/*
	 * This is supposed to be set as part of some workaround for ports that
	 * support at least PCIe Gen 4.0 speeds. As all supported platforms
	 * (Ruby, Cosmo, etc.) always support that on the port unless this
	 * is one of the WAFL related lanes, we always set this.
	 */
	if (pc->gpc_coreno != GENOA_IOMS_WAFL_PCIE_CORENO) {
		val = PCIE_PORT_LC_CTL2_SET_TS2_CHANGE_REQ(val,
		    PCIE_PORT_LC_CTL2_TS2_CHANGE_128);
	}
	genoa_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL3);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(val, 1);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * Lucky Hardware Debug 15. Why is it lucky? Because all we know is
	 * we've been told to set it.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_HW_DBG);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_HW_DBG_SET_DBG15(val, 1);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * Make sure the 8 GT/s symbols per clock is set to 2.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL6);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL6_SET_SPC_MODE_8GT(val,
	    PCIE_PORT_LC_CTL6_SPC_MODE_8GT_2);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * Software expects to see the PCIe slot implemented bit when a slot
	 * actually exists. For us, this is basically anything that actually is
	 * considered MAPPED. Set that now on the port.
	 */
	if ((port->gpp_flags & GENOA_PCIE_PORT_F_MAPPED) != 0) {
		uint16_t reg;

		reg = pci_getw_func(ioms->gio_pci_busno, port->gpp_device,
		    port->gpp_func, GENOA_BRIDGE_R_PCI_PCIE_CAP);
		reg |= PCIE_PCIECAP_SLOT_IMPL;
		pci_putw_func(ioms->gio_pci_busno, port->gpp_device,
		    port->gpp_func, GENOA_BRIDGE_R_PCI_PCIE_CAP, reg);
	}

	return (0);
}

/*
 * This is a companion to genoa_fabric_init_bridges, that operates on the PCIe
 * core level before we get to the individual bridge. This initialization
 * generally is required to ensure that each port (regardless of whether it's
 * hidden or not) is able to properly generate an all 1s response. In addition
 * we have to take care of things like atomics, idling defaults, certain
 * receiver completion buffer checks, etc.
 */
static int
genoa_fabric_init_pcie_core(genoa_pcie_core_t *pc, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_CI_CTL);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_CI_CTL_SET_LINK_DOWN_CTO_EN(val, 1);
	val = PCIE_CORE_CI_CTL_SET_IGN_LINK_DOWN_CTO_ERR(val, 1);
	genoa_pcie_core_write(pc, reg, val);

	/*
	 * Program the unit ID for this device's SDP port.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_SDP_CTL);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_SDP_CTL_SET_PORT_ID(val, pc->gpc_sdp_port);
	val = PCIE_CORE_SDP_CTL_SET_UNIT_ID(val, pc->gpc_sdp_unit);
	genoa_pcie_core_write(pc, reg, val);

	/*
	 * Program values required for receiver margining to work. These are
	 * hidden in the core. Genoa processors generally only support timing
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
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN_CTL_CAP);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_TIME(val, 1);
	genoa_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN1);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN1_SET_MAX_TIME_OFF(val, 0x32);
	val = PCIE_CORE_RX_MARGIN1_SET_NUM_TIME_STEPS(val, 0x17);
	genoa_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN2);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN2_SET_NLANES(val, 0xf);
	genoa_pcie_core_write(pc, reg, val);

	/*
	 * Ensure that RCB checking is what's seemingly expected.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PCIE_CTL);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_CTL_SET_RCB_BAD_ATTR_DIS(val, 1);
	val = PCIE_CORE_PCIE_CTL_SET_RCB_BAD_SIZE_DIS(val, 0);
	genoa_pcie_core_write(pc, reg, val);

	/*
	 * Enabling atomics in the RC requires a few different registers. Both
	 * a strap has to be overridden and then corresponding control bits.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_STRAP_F0);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_ROUTE(val, 1);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_EN(val, 1);
	genoa_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PCIE_CTL2);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_CTL2_TX_ATOMIC_ORD_DIS(val, 1);
	val = PCIE_CORE_PCIE_CTL2_TX_ATOMIC_OPS_DIS(val, 0);
	genoa_pcie_core_write(pc, reg, val);

	/*
	 * Ensure the correct electrical idle mode detection is set. In
	 * addition, it's been recommended we ignore the K30.7 EDB (EnD Bad)
	 * special symbol errors.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PCIE_P_CTL);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_P_CTL_SET_ELEC_IDLE(val,
	    PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M1);
	val = PCIE_CORE_PCIE_P_CTL_SET_IGN_EDB_ERR(val, 1);
	genoa_pcie_core_write(pc, reg, val);

	/*
	 * The IOMMUL1 does not have an instance for the on-the side WAFL lanes.
	 * Skip the WAFL port if we're that.
	 */
	if (pc->gpc_coreno >= IOMMUL1_N_PCIE_PORTS)
		return (0);

	reg = genoa_pcie_core_reg(pc, D_IOMMUL1_CTL1);
	val = genoa_pcie_core_read(pc, reg);
	val = IOMMUL1_CTL1_SET_ORDERING(val, 1);
	genoa_pcie_core_write(pc, reg, val);

	return (0);
}

typedef struct {
	genoa_ioms_t *pbc_ioms;
	uint8_t pbc_busoff;
} pci_bus_counter_t;

static int
genoa_fabric_hack_bridges_cb(genoa_pcie_port_t *port, void *arg)
{
	uint8_t bus, secbus;
	pci_bus_counter_t *pbc = arg;
	genoa_ioms_t *ioms = port->gpp_core->gpc_ioms;

	bus = ioms->gio_pci_busno;
	if (pbc->pbc_ioms != ioms) {
		pbc->pbc_ioms = ioms;
		pbc->pbc_busoff = 1 + ARRAY_SIZE(genoa_int_ports);
		for (uint_t i = 0; i < ARRAY_SIZE(genoa_int_ports); i++) {
			const genoa_pcie_port_info_t *info =
			    &genoa_int_ports[i];
			pci_putb_func(bus, info->mppi_dev, info->mppi_func,
			    PCI_BCNF_PRIBUS, bus);
			pci_putb_func(bus, info->mppi_dev, info->mppi_func,
			    PCI_BCNF_SECBUS, bus + 1 + i);
			pci_putb_func(bus, info->mppi_dev, info->mppi_func,
			    PCI_BCNF_SUBBUS, bus + 1 + i);

		}
	}

	if ((port->gpp_flags & GENOA_PCIE_PORT_F_BRIDGE_HIDDEN) != 0) {
		return (0);
	}

	secbus = bus + pbc->pbc_busoff;

	pci_putb_func(bus, port->gpp_device, port->gpp_func,
	    PCI_BCNF_PRIBUS, bus);
	pci_putb_func(bus, port->gpp_device, port->gpp_func,
	    PCI_BCNF_SECBUS, secbus);
	pci_putb_func(bus, port->gpp_device, port->gpp_func,
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
genoa_fabric_hack_bridges(genoa_fabric_t *fabric)
{
	pci_bus_counter_t c;
	bzero(&c, sizeof (c));

	genoa_fabric_walk_pcie_port(fabric, genoa_fabric_hack_bridges_cb, &c);
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
genoa_smu_hotplug_data_init(genoa_fabric_t *fabric)
{
	ddi_dma_attr_t attr;
	genoa_hotplug_t *hp = &fabric->gf_hotplug;
	const smu_hotplug_entry_t *entry;
	pfn_t pfn;
	bool cont;

	genoa_smu_dma_attr(&attr);
	hp->gh_alloc_len = MMU_PAGESIZE;
	hp->gh_table = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(hp->gh_table, MMU_PAGESIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)hp->gh_table);
	hp->gh_pa = mmu_ptob((uint64_t)pfn);

	if (genoa_board_type(fabric) == MBT_RUBY) {
		entry = ruby_hotplug_ents;
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
		genoa_iodie_t *iodie;
		genoa_ioms_t *ioms;
		genoa_pcie_core_t *pc;
		genoa_pcie_port_t *port;

		hp->gh_table->smt_map[slot] = entry[i].se_map;
		hp->gh_table->smt_func[slot] = entry[i].se_func;
		hp->gh_table->smt_reset[slot] = entry[i].se_reset;

		/*
		 * Attempt to find the port this corresponds to. It should
		 * already have been mapped.
		 */
		map = &entry[i].se_map;
		iodie = &fabric->gf_socs[map->shm_die_id].gs_iodies[0];
		ioms = &iodie->gi_ioms[map->shm_tile_id % 4];
		pc = &ioms->gio_pcie_cores[map->shm_tile_id / 4];
		port = &pc->gpc_ports[map->shm_port_id];

		cmn_err(CE_CONT, "?SMUHP: mapped entry %u to port %p\n",
		    i, port);
		VERIFY((port->gpp_flags & GENOA_PCIE_PORT_F_MAPPED) != 0);
		VERIFY0(port->gpp_flags & GENOA_PCIE_PORT_F_BRIDGE_HIDDEN);
		port->gpp_flags |= GENOA_PCIE_PORT_F_HOTPLUG;
		port->gpp_hp_type = map->shm_format;
		port->gpp_hp_slotno = slot;
		port->gpp_hp_smu_mask = entry[i].se_func.shf_mask;
	}

	return (cont);
}

/*
 * Determine the set of feature bits that should be enabled. If this is Ethanol,
 * use our hacky static versions for a moment.
 */
static uint32_t
genoa_hotplug_bridge_features(genoa_pcie_port_t *port)
{
	uint32_t feats;
	genoa_fabric_t *fabric =
	    port->gpp_core->gpc_ioms->gio_iodie->gi_soc->gs_fabric;

	if (genoa_board_type(fabric) == MBT_RUBY) {
		if (port->gpp_hp_type == SMU_HP_ENTERPRISE_SSD) {
			return (ruby_pcie_slot_cap_entssd);
		} else {
			return (ruby_pcie_slot_cap_express);
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
	switch (port->gpp_hp_type) {
	case SMU_HP_ENTERPRISE_SSD:
		/*
		 * For Enterprise SSD the set of features that are supported are
		 * considered a constant and this doesn't really vary based on
		 * the board. There is no power control, just surprise hotplug
		 * capabilities. Apparently in this mode there is no SMU command
		 * completion.
		 */
		return (feats | PCIE_SLOTCAP_NO_CMD_COMP_SUPP);
	case SMU_HP_EXPRESS_MODULE_A:
		if ((port->gpp_hp_smu_mask & SMU_ENTA_ATTNSW) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_BUTTON;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTA_EMILS) == 0 ||
		    (port->gpp_hp_smu_mask & SMU_ENTA_EMIL) == 0) {
			feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTA_PWREN) == 0) {
			feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTA_ATTNLED) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTA_PWRLED) == 0) {
			feats |= PCIE_SLOTCAP_PWR_INDICATOR;
		}
		break;
	case SMU_HP_EXPRESS_MODULE_B:
		if ((port->gpp_hp_smu_mask & SMU_ENTB_ATTNSW) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_BUTTON;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTB_EMILS) == 0 ||
		    (port->gpp_hp_smu_mask & SMU_ENTB_EMIL) == 0) {
			feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTB_PWREN) == 0) {
			feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTB_ATTNLED) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
		}

		if ((port->gpp_hp_smu_mask & SMU_ENTB_PWRLED) == 0) {
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
genoa_hotplug_bridge_post_start(genoa_pcie_port_t *port, void *arg)
{
	uint16_t ctl, sts;
	uint32_t cap;
	genoa_ioms_t *ioms = port->gpp_core->gpc_ioms;

	/*
	 * If there is no hotplug support we don't do anything here today. We
	 * assume that if we're in the simple presence mode then we still need
	 * to come through here because in theory the presence changed
	 * indicators should work.
	 */
	if ((port->gpp_flags & GENOA_PCIE_PORT_F_HOTPLUG) == 0) {
		return (0);
	}

	sts = pci_getw_func(ioms->gio_pci_busno, port->gpp_device,
	    port->gpp_func, GENOA_BRIDGE_R_PCI_SLOT_STS);
	cap = pci_getl_func(ioms->gio_pci_busno, port->gpp_device,
	    port->gpp_func, GENOA_BRIDGE_R_PCI_SLOT_CAP);

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
	ctl = pci_getw_func(ioms->gio_pci_busno, port->gpp_device,
	    port->gpp_func, GENOA_BRIDGE_R_PCI_SLOT_CTL);
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
	pci_putw_func(ioms->gio_pci_busno, port->gpp_device,
	    port->gpp_func, GENOA_BRIDGE_R_PCI_SLOT_CTL, ctl);

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
genoa_hotplug_port_init(genoa_pcie_port_t *port, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	uint32_t slot_mask;
	genoa_pcie_core_t *pc = port->gpp_core;
	genoa_ioms_t *ioms = pc->gpc_ioms;

	/*
	 * Skip over all non-hotplug slots and the simple presence mode. Though
	 * one has to ask oneself, why have hotplug if you're going to use the
	 * simple presence mode.
	 */
	if ((port->gpp_flags & GENOA_PCIE_PORT_F_HOTPLUG) == 0 ||
	    port->gpp_hp_type == SMU_HP_PRESENCE_DETECT) {
		return (0);
	}

	/*
	 * Set the hotplug slot information in the PCIe IP, presumably so that
	 * it'll do something useful for the SMU.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_HP_CTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_HP_CTL_SET_SLOT(val, port->gpp_hp_slotno);
	val = PCIE_PORT_HP_CTL_SET_ACTIVE(val, 1);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * This register is apparently set to ensure that we don't remain in the
	 * detect state machine state.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL5);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL5_SET_WAIT_DETECT(val, 0);
	genoa_pcie_port_write(port, reg, val);

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
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_TRAINBITS_DIS(val, 1);
	genoa_pcie_port_write(port, reg, val);

	/*
	 * Make sure that power faults can actually work (in theory).
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_PCTL);
	val = genoa_pcie_port_read(port, reg);
	val = PCIE_PORT_PCTL_SET_PWRFLT_EN(val, 1);
	genoa_pcie_port_write(port, reg, val);

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

	val = pci_getl_func(ioms->gio_pci_busno, port->gpp_device,
	    port->gpp_func, GENOA_BRIDGE_R_PCI_SLOT_CAP);
	val &= ~(PCIE_SLOTCAP_PHY_SLOT_NUM_MASK <<
	    PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT);
	val |= port->gpp_hp_slotno << PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT;
	val &= ~slot_mask;
	val |= genoa_hotplug_bridge_features(port);
	pci_putl_func(ioms->gio_pci_busno, port->gpp_device,
	    port->gpp_func, GENOA_BRIDGE_R_PCI_SLOT_CAP, val);

	/*
	 * Finally we need to go through and unblock training now that we've set
	 * everything else on the slot. Note, this is done before we tell the
	 * SMU about hotplug configuration, so strictly speaking devices will
	 * unlikely start suddenly training: PERST is still asserted to them on
	 * boards where that's under GPIO network control.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_SWRST_CTL6);
	val = genoa_pcie_core_read(pc, reg);
	val = bitset32(val, port->gpp_portno, port->gpp_portno, 0);
	genoa_pcie_core_write(pc, reg, val);

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
genoa_hotplug_core_init(genoa_pcie_core_t *pc, void *arg)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Nothing to do if there's no hotplug.
	 */
	if ((pc->gpc_flags & GENOA_PCIE_CORE_F_HAS_HOTPLUG) == 0) {
		return (0);
	}

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PRES);
	val = genoa_pcie_core_read(pc, reg);
	val = PCIE_CORE_PRES_SET_MODE(val, PCIE_CORE_PRES_MODE_OR);
	genoa_pcie_core_write(pc, reg, val);

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
genoa_hotplug_init(genoa_fabric_t *fabric)
{
	genoa_hotplug_t *hp = &fabric->gf_hotplug;
	genoa_iodie_t *iodie = &fabric->gf_socs[0].gs_iodies[0];

	/*
	 * These represent the addresses that we need to program in the SMU.
	 * Strictly speaking, the lower 8-bits represents the addresses that the
	 * SMU seems to expect. The upper byte is a bit more of a mystery;
	 * however, it does correspond to the expected values that AMD roughly
	 * documents for 5-bit bus segment value which is the shf_i2c_bus member
	 * of the smu_hotplug_function_t.
	 */
	const uint32_t i2c_addrs[4] = { 0x70, 0x171, 0x272, 0x373 };

	if (!genoa_smu_hotplug_data_init(fabric)) {
		/*
		 * This case is used to indicate that there was nothing in
		 * particular that needed hotplug. Therefore, we don't bother
		 * trying to tell the SMU about it.
		 */
		return (true);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(i2c_addrs); i++) {
		if (!genoa_smu_rpc_i2c_switch(iodie, i2c_addrs[i])) {
			return (false);
		}
	}

	if (!genoa_smu_rpc_give_address(iodie, MSAK_HOTPLUG, hp->gh_pa)) {
		return (false);
	}

	if (!genoa_smu_rpc_send_hotplug_table(iodie)) {
		return (false);
	}

	/*
	 * Go through now and set up bridges for hotplug data. Honor the spirit
	 * of the old world by doing this after we send the hotplug table, but
	 * before we enable things. It's unclear if the order is load bearing or
	 * not.
	 */
	(void) genoa_fabric_walk_pcie_core(fabric, genoa_hotplug_core_init,
	    NULL);
	(void) genoa_fabric_walk_pcie_port(fabric, genoa_hotplug_port_init,
	    NULL);

	if (!genoa_smu_rpc_hotplug_flags(iodie, 0)) {
		return (false);
	}

	/*
	 * This is an unfortunate bit. The SMU relies on someone else to have
	 * set the actual state of the i2c clock.
	 */
	if (!genoa_fixup_i2c_clock()) {
		return (false);
	}

	if (!genoa_smu_rpc_start_hotplug(iodie, false, 0)) {
		return (false);
	}

	/*
	 * Now that this is done, we need to go back through and do some final
	 * pieces of slot initialization which are probably necessary to get the
	 * SMU into the same place as we are with everything else.
	 */
	(void) genoa_fabric_walk_pcie_port(fabric,
	    genoa_hotplug_bridge_post_start, NULL);

	return (true);
}

#ifdef	DEBUG
static int
genoa_fabric_init_pcie_core_dbg(genoa_pcie_core_t *pc, void *arg)
{
	pc->gpc_dbg = kmem_zalloc(
	    GENOA_PCIE_DBG_SIZE(genoa_pcie_core_dbg_nregs), KM_SLEEP);
	pc->gpc_dbg->gpd_nregs = genoa_pcie_core_dbg_nregs;

	for (uint_t rn = 0; rn < pc->gpc_dbg->gpd_nregs; rn++) {
		genoa_pcie_reg_dbg_t *rd = &pc->gpc_dbg->gpd_regs[rn];

		rd->gprd_name = genoa_pcie_core_dbg_regs[rn].gprd_name;
		rd->gprd_def = genoa_pcie_core_dbg_regs[rn].gprd_def;
	}

	return (0);
}

static int
genoa_fabric_init_pcie_port_dbg(genoa_pcie_port_t *port, void *arg)
{
	port->gpp_dbg = kmem_zalloc(
	    GENOA_PCIE_DBG_SIZE(genoa_pcie_port_dbg_nregs), KM_SLEEP);
	port->gpp_dbg->gpd_nregs = genoa_pcie_port_dbg_nregs;

	for (uint_t rn = 0; rn < port->gpp_dbg->gpd_nregs; rn++) {
		genoa_pcie_reg_dbg_t *rd = &port->gpp_dbg->gpd_regs[rn];

		rd->gprd_name = genoa_pcie_port_dbg_regs[rn].gprd_name;
		rd->gprd_def = genoa_pcie_port_dbg_regs[rn].gprd_def;
	}

	return (0);
}
#endif	/* DEBUG */

/*
 * This is the main place where we basically do everything that we need to do to
 * get the PCIe engine up and running.
 */
void
genoa_fabric_init(void)
{
	genoa_fabric_t *fabric = &genoa_fabric;

	/*
	 * XXX We're missing initialization of some different pieces of the data
	 * fabric here. While some of it like scrubbing should be done as part
	 * of the memory controller driver and broader policy rather than all
	 * here right now.
	 */

	/*
	 * These register debugging facilities are costly in both space and
	 * time, and are enabled only on DEBUG kernels.
	 */
#ifdef	DEBUG
	(void) genoa_fabric_walk_pcie_core(fabric,
	    genoa_fabric_init_pcie_core_dbg, NULL);
	(void) genoa_fabric_walk_pcie_port(fabric,
	    genoa_fabric_init_pcie_port_dbg, NULL);
#endif

	/*
	 * When we come out of reset, the PSP and/or SMU have set up our DRAM
	 * routing rules and the PCI bus routing rules. We need to go through
	 * and save this information as well as set up I/O ports and MMIO. This
	 * process will also save our own allocations of these resources,
	 * allowing us to use them for our own purposes or for PCI.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_memlists, NULL);
	genoa_route_pci_bus(fabric);
	genoa_route_io_ports(fabric);
	genoa_route_mmio(fabric);

	/*
	 * While DRAM training seems to have programmed the initial memory
	 * settings our boot CPU and the DF, it is not done on the various IOMS
	 * instances. It is up to us to program that across them all.  With MMIO
	 * routed and the IOHC's understanding of TOM set up, we also want to
	 * disable the VGA MMIO hole so that the entire low memory region goes
	 * to DRAM for downstream requests just as it does from the cores.  We
	 * don't use VGA and we don't use ASeg, so there's no reason to hide
	 * this RAM from anyone.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_tom, NULL);
	genoa_fabric_walk_ioms(fabric, genoa_fabric_disable_iohc_vga, NULL);
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_iohc_pci, NULL);

	/*
	 * Let's set up PCIe. To lead off, let's make sure the system uses the
	 * right clock and let's start the process of dealing with the how
	 * configuration space retries should work, though this isn't sufficient
	 * for them to work.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_pcie_refclk, NULL);
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_pci_to, NULL);
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_iohc_features, NULL);

	/*
	 * There is a lot of different things that we have to do here. But first
	 * let me apologize in advance. The what here is weird and the why is
	 * non-existent. Effectively this is being done because either we were
	 * explicitly told to in the PPR or through other means. This is going
	 * to be weird and you have every right to complain.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_iohc_fch_link, NULL);
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_arbitration_ioms,
	    NULL);
	genoa_fabric_walk_nbif(fabric, genoa_fabric_init_arbitration_nbif,
	    NULL);
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_sdp_control, NULL);
	genoa_fabric_walk_nbif(fabric, genoa_fabric_init_nbif_syshub_dma,
	    NULL);

	/*
	 * XXX IOHC and friends clock gating.
	 */

	/*
	 * With that done, proceed to initialize the IOAPIC in each IOMS. While
	 * the FCH contains what the OS generally thinks of as the IOAPIC, we
	 * need to go through and deal with interrupt routing and how that
	 * interface with each of the northbridges here.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_ioapic, NULL);

	/*
	 * XXX For some reason programming IOHC::NB_BUS_NUM_CNTL is lopped in
	 * with the IOAPIC initialization. We may want to do this, but it can at
	 * least be its own function.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_bus_num, NULL);

	/*
	 * Go through and configure all of the straps for NBIF devices before
	 * they end up starting up.
	 *
	 * XXX There's a bunch we're punting on here and we'll want to make sure
	 * that we actually have the platform's config for this. But this
	 * includes doing things like:
	 *
	 *  o Enabling and Disabling devices visibility through straps and their
	 *    interrupt lines.
	 *  o Device multi-function enable, related PCI config space straps.
	 *  o Lots of clock gating
	 *  o Subsystem IDs
	 *  o GMI round robin
	 *  o BIFC stuff
	 */

	/* XXX Need a way to know which devs to enable on the board */
	genoa_fabric_walk_nbif(fabric, genoa_fabric_init_nbif_dev_straps, NULL);

	/*
	 * To wrap up the nBIF devices, go through and update the bridges here.
	 * We do two passes, one to get the NBIF instances and another to deal
	 * with the special instance that we believe is for the southbridge.
	 */
	genoa_fabric_walk_ioms(fabric, genoa_fabric_init_nbif_bridge, NULL);

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
	genoa_pcie_populate_dbg(&genoa_fabric, GPCS_PRE_DXIO_INIT,
	    GENOA_IODIE_MATCH_ANY);
	if (genoa_fabric_walk_iodie(fabric, genoa_dxio_init, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: lasciate ogni "
		    "speranza voi che pcie");
		return;
	}

	if (genoa_fabric_walk_iodie(fabric, genoa_dxio_plat_data, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: no platform "
		    "data");
		return;
	}

	if (genoa_fabric_walk_iodie(fabric, genoa_dxio_load_data, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to load "
		    "data into dxio");
		return;
	}

	if (genoa_fabric_walk_iodie(fabric, genoa_dxio_more_conf, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to do yet "
		    "more configuration");
		return;
	}

	genoa_pcie_populate_dbg(&genoa_fabric, GPCS_DXIO_SM_START,
	    GENOA_IODIE_MATCH_ANY);
	if (genoa_fabric_walk_iodie(fabric, genoa_dxio_state_machine, NULL) !=
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
	genoa_fabric_walk_pcie_core(fabric, genoa_fabric_init_pcie_core, NULL);
	genoa_fabric_walk_pcie_port(fabric, genoa_fabric_init_bridges, NULL);

	/*
	 * XXX This is a terrible hack. We should really fix pci_boot.c and we
	 * better before we go to market.
	 */
	genoa_fabric_hack_bridges(fabric);

	/*
	 * At this point, go talk to the SMU to actually initialize our hotplug
	 * support.
	 */
	genoa_pcie_populate_dbg(&genoa_fabric, GPCS_PRE_HOTPLUG,
	    GENOA_IODIE_MATCH_ANY);
	if (!genoa_hotplug_init(fabric)) {
		cmn_err(CE_WARN, "SMUHP: initialisation failed; PCIe hotplug "
		    "may not function properly");
	}

	genoa_pcie_populate_dbg(&genoa_fabric, GPCS_POST_HOTPLUG,
	    GENOA_IODIE_MATCH_ANY);

	/*
	 * XXX At some point, maybe not here, but before we really go too much
	 * futher we should lock all the various MMIO assignment registers,
	 * especially ones we don't intend to use.
	 */
}
