#!/usr/bin/env python3
#
# Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Generate the intc2 per-node dispatch tables.

This script runs between the two link passes (same slot as
gen_isr_tables.py). It reads pointer-free records collected in the
.intc2_list section of the pass-1 ELF and produces:

- a linker fragment (--output-linker) laying out each interrupt
  controller node's dispatch table by KEEPing the per-callsite
  .intc2_entry.<ord>.<line>.<n> input sections in line order, defining
  the __intc2_table_dts_ord_<N> symbols, and reserving zero-filled gaps
  (= spurious entries) for unconnected lines;
- a companion C file (--output-source) containing only symbol-name
  references: chain-dispatch slots for child controllers, fan-in slots
  and client directories for shared lines, and the topologically sorted
  boot directory with per-node priority records.

No addresses are ever read from the pass-1 ELF, so pass-1/pass-2 layout
differences are harmless and the scheme is LTO-compatible.
"""

import argparse
import struct
import sys

INFO_MAGIC = 0x32636E69  # "inc2"
INFO_VERSION = 1

TAG_NODE = 1
TAG_CONNECT = 2
NO_PARENT = 0xFFFFFFFF

# struct intc2_node flags (keep in sync with include/zephyr/intc2.h)
NODE_ROOT_BRIDGE = 1

REC_FIELDS = 8
REC_SIZE = REC_FIELDS * 4
INFO_SIZE = 8


class GenError(Exception):
    """Fatal input/consistency error, reported as a build error."""


class Node:
    def __init__(self, ord_, nlines, flags, parent_ord, parent_line, parent_prio):
        self.ord = ord_
        self.nlines = nlines
        self.flags = flags
        self.parent_ord = parent_ord if parent_ord != NO_PARENT else None
        self.parent_line = parent_line
        self.parent_prio = parent_prio
        # children on each of our input lines: line -> [child ord]
        self.chains = {}
        # user connects on each of our input lines: line -> [(prio, flags)]
        self.connects = {}


class Model:
    def __init__(self, nodes, boot_order, shared, dynamic, entry_size,
                 sparse_threshold=25, start_vector=0):
        self.nodes = nodes
        self.boot_order = boot_order
        self.shared = shared
        self.dynamic = dynamic
        self.entry_size = entry_size
        # Legacy tables start at GEN_IRQ_START_VECTOR: the bridge alias
        # is offset so that _sw_isr_table[vector - start] lines up with
        # the node table indexed by raw vector number.
        self.start_vector = start_vector
        # A node's table is laid out sparse when the fraction of used
        # lines is below the threshold (percent). Dynamic connect needs
        # every line addressable, so it forces dense tables.
        for node in nodes.values():
            node.bridge = bool(node.flags & NODE_ROOT_BRIDGE)
            used = len(set(node.chains) | set(node.connects))
            # A bridge node aliases _sw_isr_table: always dense/full-size
            node.sparse = (not node.bridge and not dynamic and
                           sparse_threshold > 0 and
                           used * 100 < node.nlines * sparse_threshold)
            node.used_lines = sorted(set(node.chains) | set(node.connects))
        if sum(1 for n in nodes.values() if n.bridge) > 1:
            raise GenError("intc2: more than one root-bridge node "
                           "(only one can alias _sw_isr_table)")


def parse_intlist(data, big_endian=False):
    """Parse the raw .intc2_list section into node/connect record lists."""
    fmt = (">" if big_endian else "<") + "II"

    if len(data) < INFO_SIZE:
        raise GenError("intc2: .intc2_list section too small (no header)")

    magic, version = struct.unpack_from(fmt, data, 0)
    if magic != INFO_MAGIC:
        raise GenError(f"intc2: bad .intc2_info magic 0x{magic:08x}")
    if version != INFO_VERSION:
        raise GenError(f"intc2: unsupported record version {version}")

    # The compiler may over-align the record objects (e.g. 32-byte data
    # alignment on x86-64), so the section can contain zero padding
    # between the header and records. A record never starts with a zero
    # word (tags start at 1), so zero words are unambiguously padding.
    rec_fmt = (">" if big_endian else "<") + "8I"
    word_fmt = (">" if big_endian else "<") + "I"
    node_recs = []
    conn_recs = []
    off = INFO_SIZE
    while off < len(data):
        if len(data) - off >= 4:
            (word,) = struct.unpack_from(word_fmt, data, off)
            if word == 0:
                off += 4
                continue
        if len(data) - off < REC_SIZE:
            raise GenError(f"intc2: truncated record at offset {off} of "
                           f".intc2_list ({len(data) - off} bytes left)")
        rec = struct.unpack_from(rec_fmt, data, off)
        tag = rec[0]
        if tag == TAG_NODE:
            node_recs.append(rec)
        elif tag == TAG_CONNECT:
            conn_recs.append(rec)
        else:
            raise GenError(f"intc2: unknown record tag {tag} at offset {off}")
        off += REC_SIZE

    return node_recs, conn_recs


def rehome_encoded(nodes, root, line, level_bits):
    """Resolve a multilevel-encoded line on a bridged root to the chained
    controller node it aggregates to, returning (node, local line).

    Legacy connect macros address lines behind an aggregator by their
    multilevel-encoded IRQ number; in the intc2 graph those lines belong
    to the aggregator's own node, so the connect is re-homed there.
    """
    l1_bits = level_bits[0]
    if (line >> l1_bits) == 0:
        return root, line

    node = root
    local = line & ((1 << l1_bits) - 1)
    shift = l1_bits
    for bits in level_bits[1:]:
        part = (line >> shift) & ((1 << bits) - 1)
        if part == 0:
            break
        children = node.chains.get(local, [])
        if len(children) != 1:
            raise GenError(
                f"intc2: encoded connect 0x{line:x} chains through line "
                f"{local} of node {node.ord}, which has {len(children)} "
                f"intc2 child node(s); convert the aggregating interrupt "
                f"controller's driver to intc2")
        node = nodes[children[0]]
        local = part - 1
        shift += bits
    return node, local


def build_model(node_recs, conn_recs, shared, dynamic, entry_size,
                sparse_threshold=25, start_vector=0, level_bits=None):
    """Validate records and produce the layout/boot model."""
    nodes = {}

    for (_, ord_, nlines, prio, flags, pord, pline, _r) in node_recs:
        if ord_ in nodes:
            raise GenError(f"intc2: duplicate node definition for dep "
                           f"ordinal {ord_}")
        if nlines == 0:
            raise GenError(f"intc2: node {ord_} has zero input lines")
        nodes[ord_] = Node(ord_, nlines, flags, pord, pline, prio)

    for node in nodes.values():
        if node.parent_ord is None:
            continue
        parent = nodes.get(node.parent_ord)
        if parent is None:
            raise GenError(f"intc2: node {node.ord} chains to unknown "
                           f"controller (dep ordinal {node.parent_ord})")
        if node.parent_line >= parent.nlines:
            raise GenError(f"intc2: node {node.ord} chains to line "
                           f"{node.parent_line} of node {parent.ord}, which "
                           f"only has {parent.nlines} lines")
        if node.parent_prio > 0xFF:
            raise GenError(f"intc2: node {node.ord} parent priority "
                           f"{node.parent_prio} exceeds 255")
        parent.chains.setdefault(node.parent_line, []).append(node.ord)

    for (_, ord_, line, prio, flags, _po, _pl, order) in conn_recs:
        node = nodes.get(ord_)
        if node is None:
            raise GenError(f"intc2: INTC2_DT_CONNECT targets unknown "
                           f"controller (dep ordinal {ord_})")
        # Section names carry the callsite identity (the connect macro's
        # target ord/line, assembler-evaluated); a re-homed connect keeps
        # its original name, so compute it before decoding.
        name = f".intc2_entry.{ord_}.{line}.{order}"
        if level_bits and (node.flags & NODE_ROOT_BRIDGE):
            node, line = rehome_encoded(nodes, node, line, level_bits)
        if line >= node.nlines:
            raise GenError(f"intc2: connect to line {line} of node "
                           f"{node.ord}, which only has {node.nlines} lines")
        if prio > 0xFF or flags > 0xFF:
            raise GenError(f"intc2: connect on node {node.ord} line {line}: "
                           f"priority/flags must fit in 8 bits")
        node.connects.setdefault(line, []).append((order, prio, flags, name))

    # Registration order within a translation unit is the __COUNTER__
    # value recorded in the connect record; compilers may emit
    # same-section objects in any order, so this - not section order -
    # defines client ordering.
    for node in nodes.values():
        for conns in node.connects.values():
            conns.sort(key=lambda c: c[0])

    # A line is shared when the total client count (chain dispatchers +
    # user connects) exceeds one.
    if not shared:
        for node in nodes.values():
            lines = set(node.chains) | set(node.connects)
            for line in lines:
                count = len(node.chains.get(line, [])) + \
                    len(node.connects.get(line, []))
                if count > 1:
                    raise GenError(
                        f"intc2: line {line} of node {node.ord} has {count} "
                        f"clients; enable CONFIG_INTC2_SHARED to allow "
                        f"shared lines")

    # Topological order over parent edges, roots first (Kahn).
    indeg = {ord_: 0 for ord_ in nodes}
    for node in nodes.values():
        if node.parent_ord is not None:
            indeg[node.ord] += 1
    queue = sorted(o for o, d in indeg.items() if d == 0)
    order = []
    children = {}
    for node in nodes.values():
        if node.parent_ord is not None:
            children.setdefault(node.parent_ord, []).append(node.ord)
    while queue:
        ord_ = queue.pop(0)
        order.append(ord_)
        for child in sorted(children.get(ord_, [])):
            indeg[child] -= 1
            if indeg[child] == 0:
                queue.append(child)
    if len(order) != len(nodes):
        cyclic = sorted(o for o, d in indeg.items() if d > 0)
        raise GenError(f"intc2: cycle in the interrupt graph involving dep "
                       f"ordinals {cyclic}")

    return Model(nodes, order, shared, dynamic, entry_size, sparse_threshold,
                 start_vector)


def line_clients(node, line):
    """Number of clients on a (node, line) input."""
    return len(node.chains.get(line, [])) + len(node.connects.get(line, []))


def emit_linker(model):
    """Emit the table-layout linker fragment (included in the final pass)."""
    out = []
    out.append("/* Generated by gen_intc2_tables.py - do not edit */")
    out.append("FILL(0x00);")
    align = 8 if model.entry_size == 16 else 4

    def entry_keeps(node, line):
        """Exact-name KEEPs for a line's clients in registration order:
        chain dispatchers first, then user connects sorted by their
        recorded __COUNTER__ order."""
        keeps = []
        for k in range(len(node.chains.get(line, []))):
            keeps.append(f"KEEP(*(.intc2_slot.{node.ord}.{line}.{k}))")
        for (_order, _prio, _flags, name) in node.connects.get(line, []):
            keeps.append(f"KEEP(*({name}))")
        return keeps

    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        out.append(f". = ALIGN({align});")
        out.append(f"__intc2_table_dts_ord_{ord_} = .;")
        # alias for toolchains that prefix C symbols with an underscore
        # (only materializes when referenced, harmless elsewhere)
        out.append(f"PROVIDE(___intc2_table_dts_ord_{ord_} = __intc2_table_dts_ord_{ord_});")
        if node.bridge:
            off = model.start_vector * model.entry_size
            out.append(f"_sw_isr_table = __intc2_table_dts_ord_{ord_} + {off};")
            out.append(f"PROVIDE(__sw_isr_table = _sw_isr_table);")
        lines = node.used_lines if node.sparse else range(node.nlines)
        nslots = 0
        for line in lines:
            count = line_clients(node, line)
            nslots += 1
            if count == 0:
                if node.bridge:
                    out.append(f"KEEP(*(.intc2_spur.{ord_}.{line})) /* line {line} */")
                else:
                    out.append(f". = . + {model.entry_size}; /* line {line}: spurious */")
            elif count == 1:
                out.append(f"{entry_keeps(node, line)[0]} /* line {line} */")
            else:
                out.append(f"KEEP(*(.intc2_fanin_slot.{ord_}.{line})) "
                           f"/* line {line}: {count} clients */")
        # One entry per laid-out line, exactly: a missed KEEP (name
        # drift) or a duplicated entry emission fails the link instead
        # of silently shifting the table layout.
        out.append(f"ASSERT((. - __intc2_table_dts_ord_{ord_}) == "
                   f"{nslots * model.entry_size}, "
                   f"\"intc2: node {ord_} dispatch table layout mismatch\");")

    # Client directories of shared lines, contiguous per line, in
    # registration order.
    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        for line in range(node.nlines):
            if line_clients(node, line) < 2:
                continue
            out.append(f". = ALIGN({align});")
            out.append(f"__intc2_fanin_cl_{ord_}_{line} = .;")
            out.append(f"PROVIDE(___intc2_fanin_cl_{ord_}_{line} = "
                       f"__intc2_fanin_cl_{ord_}_{line});")
            out.extend(entry_keeps(node, line))

    # Safety net: any stray entry/slot section that validation did not
    # account for still gets placed (never silently orphaned).
    out.append("KEEP(*(.intc2_entry.*))")
    out.append("KEEP(*(.intc2_slot.*))")
    out.append("KEEP(*(.intc2_fanin_slot.*))")
    out.append("KEEP(*(.intc2_spur.*))")
    out.append("")
    return "\n".join(out)


def emit_source(model):
    """Emit the companion C file (symbol references only, no addresses)."""
    out = []
    out.append("/* Generated by gen_intc2_tables.py - do not edit */")
    out.append("")
    out.append("#include <zephyr/intc2.h>")
    out.append("")

    # Spurious filler entries for the empty lines of a root-bridge
    # node, matching the legacy table's z_irq_spurious default.
    if any(n.bridge for n in model.nodes.values()):
        out.append("extern void z_irq_spurious(const void *unused);")
        out.append("")
    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        if not node.bridge:
            continue
        for line in range(node.nlines):
            if line_clients(node, line) != 0:
                continue
            out.append(f"Z_INTC2_TABLE_CONST struct intc2_entry __intc2_spur_{ord_}_{line}")
            out.append(f"\t__attribute__((section(\".intc2_spur.{ord_}.{line}\")))")
            out.append(f"\t__used = {{ .arg = NULL, .isr = z_irq_spurious }};")
        out.append("")

    # Sparse-table line directories: lines[0] is the count, then the
    # used line numbers ascending, matching the table layout order.
    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        if not node.sparse:
            continue
        used = ", ".join(str(line) for line in node.used_lines)
        out.append(f"const uint16_t __intc2_lines_dts_ord_{ord_}[] = {{")
        out.append(f"\t{len(node.used_lines)}, {used}")
        out.append(f"}};")
        out.append("")

    # Chain-dispatch slots: the child controller's dispatch entry placed
    # on its parent's input line.
    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        for line in sorted(node.chains):
            for k, child in enumerate(node.chains[line]):
                out.append(f"extern const struct intc2_node __intc2_node_dts_ord_{child};")
                out.append(f"Z_INTC2_TABLE_CONST struct intc2_entry")
                out.append(f"__intc2_slot_p{ord_}_l{line}_c{child}")
                out.append(f"\t__attribute__((section(\".intc2_slot.{ord_}.{line}.{k}\")))")
                out.append(f"\t__used = {{")
                out.append(f"\t\t.arg = &__intc2_node_dts_ord_{child},")
                out.append(f"\t\t.isr = z_intc2_node_dispatch,")
                out.append(f"}};")
                out.append("")

    # Fan-in slots + client directory descriptors for shared lines.
    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        for line in range(node.nlines):
            count = line_clients(node, line)
            if count < 2:
                continue
            out.append(f"extern const struct intc2_entry __intc2_fanin_cl_{ord_}_{line}[];")
            out.append(f"static const struct z_intc2_fanin __intc2_fanin_{ord_}_{line} = {{")
            out.append(f"\t.clients = __intc2_fanin_cl_{ord_}_{line},")
            out.append(f"\t.count = {count},")
            out.append(f"}};")
            out.append(f"Z_INTC2_TABLE_CONST struct intc2_entry")
            out.append(f"__intc2_fanin_slot_{ord_}_{line}")
            out.append(f"\t__attribute__((section(\".intc2_fanin_slot.{ord_}.{line}\")))")
            out.append(f"\t__used = {{")
            out.append(f"\t\t.arg = &__intc2_fanin_{ord_}_{line},")
            out.append(f"\t\t.isr = z_intc2_fanin_isr,")
            out.append(f"}};")
            out.append("")

    # Boot directory: topological order, priority records per node. A
    # child's chain-line priority is programmed on the *parent*.
    for ord_ in model.boot_order:
        node = model.nodes[ord_]
        recs = []
        for line in sorted(node.connects):
            for (_order, prio, flags, _name) in node.connects[line]:
                recs.append((line, prio, flags))
        for line in sorted(node.chains):
            for child in node.chains[line]:
                child_node = model.nodes[child]
                recs.append((line, child_node.parent_prio, 0))
        if recs:
            out.append(f"static const struct z_intc2_prio_rec __intc2_prio_{ord_}[] = {{")
            for (line, prio, flags) in recs:
                out.append(f"\t{{ .line = {line}, .prio = {prio}, .flags = {flags} }},")
            out.append(f"}};")
        node.prio_count = len(recs)

    out.append("")
    if model.boot_order:
        for ord_ in model.boot_order:
            out.append(f"extern const struct intc2_node __intc2_node_dts_ord_{ord_};")
        out.append("const struct z_intc2_boot_rec __intc2_boot[] = {")
        for ord_ in model.boot_order:
            node = model.nodes[ord_]
            recs = f"__intc2_prio_{ord_}" if node.prio_count else "NULL"
            out.append(f"\t{{ .node = &__intc2_node_dts_ord_{ord_}, "
                       f".recs = {recs}, .count = {node.prio_count} }},")
        out.append("};")
    out.append(f"const uint32_t __intc2_boot_cnt = {len(model.boot_order)};")
    out.append("")
    return "\n".join(out)


def read_intlist(path, section_names):
    from elftools.elf.elffile import ELFFile

    with open(path, "rb") as fp:
        elf = ELFFile(fp)

        for name in section_names:
            section = elf.get_section_by_name(name)
            if section is not None:
                return section.data()

    raise GenError(f"intc2: no {section_names} section in {path}")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     allow_abbrev=False)
    parser.add_argument("--kernel", required=True,
                        help="Pass-1 (zephyr_pre*) ELF file")
    parser.add_argument("--intlist-section", action="append", required=True,
                        help="The name of the .intc2_list section (can be "
                             "repeated; the first section found is used)")
    parser.add_argument("--output-source", required=True,
                        help="Generated C companion file")
    parser.add_argument("--output-linker", required=True,
                        help="Generated linker fragment")
    parser.add_argument("--big-endian", action="store_true",
                        help="Target is big-endian")
    parser.add_argument("--shared", action="store_true",
                        help="CONFIG_INTC2_SHARED is enabled")
    parser.add_argument("--dynamic", action="store_true",
                        help="CONFIG_INTC2_DYNAMIC is enabled")
    parser.add_argument("--entry-size", type=int, default=8, choices=(8, 16),
                        help="sizeof(struct intc2_entry) on the target")
    parser.add_argument("--start-vector", type=int, default=0,
                        help="CONFIG_GEN_IRQ_START_VECTOR: offset of the "
                             "legacy table alias on a root-bridge node")
    parser.add_argument("--level-bits", type=lambda s: tuple(
                            int(b) for b in s.split(",")),
                        default=None,
                        help="Comma-separated CONFIG_*_LEVEL_INTERRUPT_BITS "
                             "widths when CONFIG_MULTI_LEVEL_INTERRUPTS is "
                             "enabled; multilevel-encoded connects on the "
                             "bridged root are re-homed to the aggregating "
                             "controller's node")
    parser.add_argument("--sparse-threshold", type=int, default=25,
                        help="Lay a node's table out sparse when fewer than "
                             "this percentage of its lines are used "
                             "(0 disables sparse tables)")
    parser.add_argument("--debug", action="store_true",
                        help="Print debug information")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    try:
        data = read_intlist(args.kernel, args.intlist_section)
        node_recs, conn_recs = parse_intlist(data, args.big_endian)

        model = build_model(node_recs, conn_recs, args.shared, args.dynamic,
                            args.entry_size, args.sparse_threshold,
                            args.start_vector, args.level_bits)
    except GenError as err:
        sys.exit(str(err))

    if args.debug:
        for ord_ in model.boot_order:
            node = model.nodes[ord_]
            print(f"intc2: node ord {ord_}: nlines {node.nlines} "
                  f"parent {node.parent_ord}:{node.parent_line} "
                  f"connects {sorted(node.connects)} chains {sorted(node.chains)}")

    with open(args.output_linker, "w") as fp:
        fp.write(emit_linker(model))
    with open(args.output_source, "w") as fp:
        fp.write(emit_source(model))


if __name__ == "__main__":
    main()
