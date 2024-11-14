:orphan:

.. _migration_4.1:

Migration guide to Zephyr v4.1.0 (Working Draft)
################################################

This document describes the changes required when migrating your application from Zephyr v4.0.0 to
Zephyr v4.1.0.

Any other changes (not directly related to migrating applications) can be found in
the :ref:`release notes<zephyr_4.1>`.

.. contents::
    :local:
    :depth: 2

Build System
************

Kernel
******

Boards
******

Modules
*******

Mbed TLS
========

Trusted Firmware-M
==================

LVGL
====

Device Drivers and Devicetree
*****************************

Controller Area Network (CAN)
=============================

Display
=======

Enhanced Serial Peripheral Interface (eSPI)
===========================================

GNSS
====

Input
=====

Interrupt Controller
====================

LED Strip
=========

Sensors
=======

Serial
======

Regulator
=========

Bluetooth
*********

Bluetooth HCI
=============

Bluetooth Mesh
==============

Bluetooth Audio
===============

Bluetooth Classic
=================

Bluetooth Host
==============

Bluetooth Crypto
================

Networking
**********

Other Subsystems
****************

Flash map
=========

hawkBit
=======

MCUmgr
======

Modem
=====

Architectures
*************

RISC V
======

* The :kconfig:option:`CONFIG_RISCV_HART_MASK` Kconfig has been removed. For ``arch_proc_id()`` calls
  that expects the return processor ID to start 0, use ``arch_curr_proc()->id`` instead.
