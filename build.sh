#!/bin/bash

# SPDX-FileCopyrightText: 2026 ACCES I/O Products, Inc.
#
# SPDX-License-Identifier: GPL-2.0-only

make EXTRA_CFLAGS="-DA_PCI_DEBUG -DDEBUG -I. " 2>&1 
