/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Global variables from PSA Crypto HSE driver,
 * should be put in non-cacheable region
 */
*(.bss.pAlignKeyLen0)
*(.bss.pAlignKeyLen1)
*(.bss.pAlignKeyLen2)
*(.bss.pAlignSignLen0)
*(.bss.pAlignSignLen1)
*(.bss.pAlignCipherLen)
