/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Global variables in psa_demo, should be put in non-cacheable region */
*(.bss.len)
*(.bss.input_len)
*(.bss.encrypted_len)
*(.bss.decrypted_len)
*(.bss.input_data)
*(.bss.encrypted_data)
*(.bss.buf)
*(.bss.input)
*(.bss.output)
*(.bss._mbedtls_heap)

/* Put main stack to non-cacheable region */
*(".noinit.*.0")
