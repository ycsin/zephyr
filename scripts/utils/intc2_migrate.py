#!/usr/bin/env python3
#
# Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Migrate legacy interrupt call sites to the intc2 API.

Conservatively rewrites, in the given files/directories:

  IRQ_CONNECT(DT_INST_IRQN(n), p, isr, arg, f)
      -> INTC2_DT_INST_CONNECT_INLINE(n, p, isr, arg, f)
  IRQ_CONNECT(DT_INST_IRQN_BY_IDX(n, i), ...)
      -> INTC2_DT_INST_CONNECT_INLINE_BY_IDX(n, i, ...)
  IRQ_CONNECT(DT_IRQN(node), ...)          -> INTC2_DT_CONNECT_INLINE(node, ...)
  IRQ_CONNECT(DT_IRQN_BY_IDX(node, i), ..) -> INTC2_DT_CONNECT_INLINE_BY_IDX(...)
  IRQ_CONNECT(DT_IRQN_BY_NAME(node, nm), ...)
      -> INTC2_DT_CONNECT_INLINE_BY_NAME(node, nm, ...)
  irq_enable/irq_disable/irq_is_enabled(<DT irqn expr>)
      -> intc2_enable/... ((struct intc2_spec)INTC2_..._SPEC_GET...)

and inserts `#include <zephyr/intc2.h>` when a rewrite happened. Call
sites whose first argument is not one of the recognized devicetree IRQ
number forms are left untouched and reported, as are IRQ_DIRECT_CONNECT
and irq_connect_dynamic sites (they stay on the legacy API for now).

The INLINE connect forms are call-position-compatible with the legacy
macros on both intc2 backends, so this transformation is semantics
preserving wherever it applies.
"""

import argparse
import os
import re
import sys

IDENT = re.compile(r"[A-Za-z0-9_]")

# (outer callable, spec/connect head chooser by arg0 head)
ARG0_MAP = [
    (re.compile(r"^DT_INST_IRQN_BY_IDX\s*\((.*)\)$", re.S),
     "INTC2_DT_INST_CONNECT_INLINE_BY_IDX", "INTC2_DT_INST_SPEC_GET_BY_IDX"),
    (re.compile(r"^DT_INST_IRQN\s*\((.*)\)$", re.S),
     "INTC2_DT_INST_CONNECT_INLINE", "INTC2_DT_INST_SPEC_GET"),
    (re.compile(r"^DT_IRQN_BY_IDX\s*\((.*)\)$", re.S),
     "INTC2_DT_CONNECT_INLINE_BY_IDX", "INTC2_DT_SPEC_GET_BY_IDX"),
    (re.compile(r"^DT_IRQN_BY_NAME\s*\((.*)\)$", re.S),
     "INTC2_DT_CONNECT_INLINE_BY_NAME", "INTC2_DT_SPEC_GET_BY_NAME"),
    (re.compile(r"^DT_IRQN\s*\((.*)\)$", re.S),
     "INTC2_DT_CONNECT_INLINE", "INTC2_DT_SPEC_GET"),
]

ENABLE_MAP = {
    "irq_enable": "intc2_enable",
    "irq_disable": "intc2_disable",
    "irq_is_enabled": "intc2_is_enabled",
}


def find_calls(text, name):
    """Yield (start, args_start, end) for each `name(...)` call, where
    end is the index one past the closing parenthesis."""
    idx = 0
    while True:
        idx = text.find(name, idx)
        if idx < 0:
            return
        before = text[idx - 1] if idx > 0 else ""
        after = idx + len(name)
        if IDENT.match(before) or (after < len(text) and IDENT.match(text[after])):
            idx = after
            continue
        # skip whitespace to the opening parenthesis
        par = after
        while par < len(text) and text[par] in " \t\\\n":
            par += 1
        if par >= len(text) or text[par] != "(":
            idx = after
            continue
        depth = 0
        for end in range(par, len(text)):
            if text[end] == "(":
                depth += 1
            elif text[end] == ")":
                depth -= 1
                if depth == 0:
                    yield (idx, par + 1, end + 1)
                    break
        else:
            return
        idx = end + 1


def split_args(argtext):
    """Split top-level comma-separated arguments, preserving verbatim
    text (including line continuations and comments)."""
    args = []
    depth = 0
    cur = []
    for ch in argtext:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    args.append("".join(cur))
    return args


def match_arg0(arg0):
    stripped = arg0.strip().replace("\\\n", "").strip()
    for (rx, connect_head, spec_head) in ARG0_MAP:
        m = rx.match(stripped)
        if m:
            return connect_head, spec_head, m.group(1).strip()
    return None


def rewrite(text, stats):
    out = []
    changed = False

    # IRQ_CONNECT rewrites
    pos = 0
    for (start, args_start, end) in list(find_calls(text, "IRQ_CONNECT")):
        # skip ARCH_IRQ_CONNECT / IRQ_DIRECT_CONNECT via identifier check
        # (find_calls already enforces word boundaries)
        args = split_args(text[args_start:end - 1])
        if len(args) != 5:
            stats["residual"].append("IRQ_CONNECT (unexpected argument count)")
            continue
        m = match_arg0(args[0])
        if m is None:
            stats["residual"].append(f"IRQ_CONNECT({args[0].strip()[:40]}...)")
            continue
        connect_head, _, inner = m
        rest = ",".join(args[1:])
        out.append(text[pos:start])
        out.append(f"{connect_head}({inner},{rest})")
        pos = end
        changed = True
        stats["connect"] += 1
    out.append(text[pos:])
    text = "".join(out)

    # irq_enable/disable/is_enabled rewrites
    for legacy, new in ENABLE_MAP.items():
        out = []
        pos = 0
        for (start, args_start, end) in list(find_calls(text, legacy)):
            args = split_args(text[args_start:end - 1])
            if len(args) != 1:
                continue
            m = match_arg0(args[0])
            if m is None:
                stats["residual"].append(f"{legacy}({args[0].strip()[:40]})")
                continue
            _, spec_head, inner = m
            out.append(text[pos:start])
            out.append(f"{new}((struct intc2_spec){spec_head}({inner}))")
            pos = end
            changed = True
            stats["enable"] += 1
        out.append(text[pos:])
        text = "".join(out)

    if changed and "#include <zephyr/intc2.h>" not in text:
        # insert after the first zephyr include block line
        m = re.search(r"^#include <zephyr/[^>]+>.*$", text, re.M)
        if m:
            text = text[:m.end()] + "\n#include <zephyr/intc2.h>" + text[m.end():]
        else:
            text = "#include <zephyr/intc2.h>\n" + text

    return text, changed


def collect_files(paths):
    for path in paths:
        if os.path.isfile(path):
            yield path
        else:
            for root, _dirs, files in os.walk(path):
                for f in sorted(files):
                    if f.endswith(".c"):
                        yield os.path.join(root, f)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="Files or directories to migrate")
    parser.add_argument("--dry-run", action="store_true",
                        help="Report what would change without writing")
    args = parser.parse_args()

    total = {"files": 0, "changed": 0, "connect": 0, "enable": 0, "residual": 0}

    for path in collect_files(args.paths):
        with open(path, encoding="utf-8", errors="surrogateescape") as fp:
            text = fp.read()
        if "IRQ_CONNECT" not in text and not any(k in text for k in ENABLE_MAP):
            continue
        stats = {"connect": 0, "enable": 0, "residual": []}
        new_text, changed = rewrite(text, stats)
        total["files"] += 1
        if changed:
            total["changed"] += 1
            total["connect"] += stats["connect"]
            total["enable"] += stats["enable"]
            if not args.dry_run:
                with open(path, "w", encoding="utf-8",
                          errors="surrogateescape") as fp:
                    fp.write(new_text)
        if stats["residual"]:
            total["residual"] += len(stats["residual"])
            for r in stats["residual"]:
                print(f"residual: {path}: {r}")

    print(f"intc2_migrate: {total['changed']}/{total['files']} files changed, "
          f"{total['connect']} connects, {total['enable']} enables, "
          f"{total['residual']} residual sites")


if __name__ == "__main__":
    main()
