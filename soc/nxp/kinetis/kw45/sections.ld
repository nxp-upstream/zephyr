/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* NOINIT section for rpmsg_sh_mem */
.noinit_rpmsg_sh_mem (NOLOAD) : ALIGN(4)
{
   __RPMSG_SH_MEM_START__ = .;
   *(.noinit.$rpmsg_sh_mem*)
   . = ALIGN(4) ;
   __RPMSG_SH_MEM_END__ = .;
} > rpmsg_sh_mem
