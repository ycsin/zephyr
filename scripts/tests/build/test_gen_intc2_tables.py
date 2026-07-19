#!/usr/bin/env python3
# Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for gen_intc2_tables.py"""

import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.environ.get("ZEPHYR_BASE",
                                               os.path.join(os.path.dirname(__file__),
                                                            "..", "..", "..")),
                                "scripts", "build"))
import gen_intc2_tables as gen  # Implementation Under Test


def pack_header(magic=gen.INFO_MAGIC, version=gen.INFO_VERSION):
    return struct.pack("<II", magic, version)


def pack_rec(tag, ord_, lon, prio=0, flags=0, pord=gen.NO_PARENT, pline=0):
    return struct.pack("<8I", tag, ord_, lon, prio, flags, pord, pline, 0)


def node_rec(ord_, nlines, pord=gen.NO_PARENT, pline=0, pprio=0, flags=0):
    """A NODE record tuple as produced by parse_intlist()."""
    return (gen.TAG_NODE, ord_, nlines, pprio, flags, pord, pline, 0)


def conn_rec(ord_, line, prio=0, flags=0, order=0):
    """A CONNECT record tuple as produced by parse_intlist()."""
    return (gen.TAG_CONNECT, ord_, line, prio, flags, 0, 0, order)


def build(nodes, conns, shared=False, dynamic=False, entry_size=8):
    return gen.build_model(nodes, conns, shared, dynamic, entry_size)


class TestParse:
    def test_roundtrip(self):
        data = pack_header() + \
            pack_rec(gen.TAG_NODE, 5, 8) + \
            pack_rec(gen.TAG_CONNECT, 5, 3, prio=7, flags=1)
        node_recs, conn_recs = gen.parse_intlist(data)
        assert len(node_recs) == 1
        assert len(conn_recs) == 1
        assert node_recs[0][1] == 5
        assert conn_recs[0][2] == 3

    def test_big_endian(self):
        data = struct.pack(">II", gen.INFO_MAGIC, gen.INFO_VERSION) + \
            struct.pack(">8I", gen.TAG_NODE, 5, 8, 0, 0, gen.NO_PARENT, 0, 0)
        node_recs, conn_recs = gen.parse_intlist(data, big_endian=True)
        assert node_recs[0][1] == 5
        assert not conn_recs

    def test_bad_magic(self):
        with pytest.raises(gen.GenError, match="magic"):
            gen.parse_intlist(pack_header(magic=0xdeadbeef))

    def test_bad_version(self):
        with pytest.raises(gen.GenError, match="version"):
            gen.parse_intlist(pack_header(version=99))

    def test_truncated_body(self):
        with pytest.raises(gen.GenError, match="truncated"):
            gen.parse_intlist(pack_header() + struct.pack("<I", gen.TAG_NODE))

    def test_alignment_padding_skipped(self):
        # 24 bytes of padding after the 8-byte header (32-byte aligned
        # records, as produced on x86-64) plus inter-record padding
        data = pack_header() + b"\x00" * 24 + \
            pack_rec(gen.TAG_NODE, 5, 8) + b"\x00" * 8 + \
            pack_rec(gen.TAG_CONNECT, 5, 3)
        node_recs, conn_recs = gen.parse_intlist(data)
        assert len(node_recs) == 1
        assert len(conn_recs) == 1

    def test_unknown_tag(self):
        with pytest.raises(gen.GenError, match="tag"):
            gen.parse_intlist(pack_header() + pack_rec(42, 0, 0))


class TestValidation:
    def test_duplicate_node(self):
        with pytest.raises(gen.GenError, match="duplicate node"):
            build([node_rec(5, 8), node_rec(5, 4)], [])

    def test_connect_unknown_node(self):
        with pytest.raises(gen.GenError, match="unknown controller"):
            build([node_rec(5, 8)], [conn_rec(6, 0)])

    def test_connect_line_out_of_range(self):
        with pytest.raises(gen.GenError, match="only has 8 lines"):
            build([node_rec(5, 8)], [conn_rec(5, 8)])

    def test_parent_unknown(self):
        with pytest.raises(gen.GenError, match="unknown controller"):
            build([node_rec(12, 8, pord=5, pline=3)], [])

    def test_parent_line_out_of_range(self):
        with pytest.raises(gen.GenError, match="only has 8 lines"):
            build([node_rec(5, 8), node_rec(12, 8, pord=5, pline=8)], [])

    def test_fanin_needs_shared(self):
        with pytest.raises(gen.GenError, match="CONFIG_INTC2_SHARED"):
            build([node_rec(5, 8)], [conn_rec(5, 3), conn_rec(5, 3)])

    def test_fanin_with_shared_ok(self):
        model = build([node_rec(5, 8)], [conn_rec(5, 3), conn_rec(5, 3)],
                      shared=True)
        assert gen.line_clients(model.nodes[5], 3) == 2

    def test_chain_slot_collision_needs_shared(self):
        nodes = [node_rec(5, 8), node_rec(12, 4, pord=5, pline=3)]
        with pytest.raises(gen.GenError, match="CONFIG_INTC2_SHARED"):
            build(nodes, [conn_rec(5, 3)])

    def test_cycle(self):
        nodes = [node_rec(5, 8, pord=12, pline=0),
                 node_rec(12, 8, pord=5, pline=0)]
        with pytest.raises(gen.GenError, match="cycle"):
            build(nodes, [])

    def test_prio_range(self):
        with pytest.raises(gen.GenError, match="8 bits"):
            build([node_rec(5, 8)], [conn_rec(5, 3, prio=256)])


class TestTopoOrder:
    def test_roots_first(self):
        nodes = [node_rec(30, 4, pord=12, pline=1),
                 node_rec(12, 8, pord=5, pline=3),
                 node_rec(5, 8)]
        model = build(nodes, [])
        assert model.boot_order == [5, 12, 30]

    def test_two_roots(self):
        nodes = [node_rec(9, 4), node_rec(5, 8),
                 node_rec(12, 8, pord=5, pline=3)]
        model = build(nodes, [])
        assert model.boot_order.index(5) < model.boot_order.index(12)
        assert set(model.boot_order) == {5, 9, 12}


class TestEmitLinker:
    def test_dense_layout(self):
        model = build([node_rec(5, 4)], [conn_rec(5, 1, order=4)])
        ld = gen.emit_linker(model)
        assert "__intc2_table_dts_ord_5 = .;" in ld
        assert "KEEP(*(.intc2_entry.5.1.4))" in ld
        # lines 0, 2, 3 are gaps
        assert ld.count(". = . + 8;") == 3

    def test_entry_size_64bit(self):
        model = build([node_rec(5, 2)], [], entry_size=16)
        ld = gen.emit_linker(model)
        assert ". = . + 16;" in ld
        assert ". = ALIGN(8);" in ld

    def test_chain_slot_placement(self):
        model = build([node_rec(5, 8), node_rec(12, 4, pord=5, pline=3)], [])
        ld = gen.emit_linker(model)
        assert "KEEP(*(.intc2_slot.5.3.0))" in ld

    def test_fanin_layout(self):
        model = build([node_rec(5, 8)],
                      [conn_rec(5, 3, order=0), conn_rec(5, 3, order=1)],
                      shared=True)
        ld = gen.emit_linker(model)
        assert "KEEP(*(.intc2_fanin_slot.5.3))" in ld
        assert "__intc2_fanin_cl_5_3 = .;" in ld

    def test_fanin_registration_order(self):
        # records arrive in arbitrary (e.g. compiler-reversed) order;
        # emission must follow the recorded __COUNTER__ order
        model = build([node_rec(5, 8)],
                      [conn_rec(5, 3, prio=9, order=7),
                       conn_rec(5, 3, prio=4, order=2)], shared=True)
        ld = gen.emit_linker(model)
        cl = ld[ld.index("__intc2_fanin_cl_5_3"):]
        assert cl.index("KEEP(*(.intc2_entry.5.3.2))") < \
            cl.index("KEEP(*(.intc2_entry.5.3.7))")
        # priority application follows the same order: 4 first, 9 last
        src = gen.emit_source(model)
        assert src.index(".prio = 4") < src.index(".prio = 9")

    def test_catch_all_tail(self):
        model = build([node_rec(5, 1)], [])
        ld = gen.emit_linker(model)
        assert "KEEP(*(.intc2_entry.*))" in ld
        assert "KEEP(*(.intc2_slot.*))" in ld


class TestEmitSource:
    def test_chain_slot(self):
        model = build([node_rec(5, 8), node_rec(12, 4, pord=5, pline=3, pprio=6)], [])
        src = gen.emit_source(model)
        assert '__attribute__((section(".intc2_slot.5.3.0")))' in src
        assert "&__intc2_node_dts_ord_12" in src
        assert "z_intc2_node_dispatch" in src
        # child's chain priority is programmed on the parent's line
        assert "__intc2_prio_5" in src
        assert "{ .line = 3, .prio = 6, .flags = 0 }" in src

    def test_fanin_source(self):
        model = build([node_rec(5, 8)],
                      [conn_rec(5, 3), conn_rec(5, 3)], shared=True)
        src = gen.emit_source(model)
        assert "__intc2_fanin_cl_5_3" in src
        assert ".count = 2," in src
        assert "z_intc2_fanin_isr" in src

    def test_boot_directory(self):
        model = build([node_rec(5, 8), node_rec(12, 4, pord=5, pline=3)],
                      [conn_rec(12, 2, prio=9)])
        src = gen.emit_source(model)
        assert "const uint32_t __intc2_boot_cnt = 2;" in src
        boot = src[src.index("__intc2_boot[]"):]
        assert boot.index("&__intc2_node_dts_ord_5") < \
            boot.index("&__intc2_node_dts_ord_12,")
        assert "{ .line = 2, .prio = 9, .flags = 0 }" in src

    def test_empty_model(self):
        model = build([], [])
        src = gen.emit_source(model)
        assert "__intc2_boot_cnt = 0;" in src
        assert "__intc2_boot[]" not in src
