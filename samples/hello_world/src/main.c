/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

int test_Hpram_space(void)
{
    /* test ram space. */
    uint32_t *hyperram_start = (uint32_t *)0x70000000;
    uint32_t hyperram_size   = 0x02000000/4;  //32M
    uint32_t i;
    uint32_t temp;

    for(i = 0; i < hyperram_size; ++i)
    {
        hyperram_start[i] = i;
    }
    for(i = 0; i < hyperram_size; ++i)
    {
        temp = hyperram_start[i];
        if(i != temp)
        {
            printf("WRONG HAPPENED! address:0x%08x, value:0x%08x, i:0x%08x\r\n", &hyperram_start[i], temp, i);
            return -1;
        }
    }

    return 0;
}

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	uint32_t status = test_Hpram_space();

	if (status == 0)
	{
		printf("hyperram test success");
	}

	return 0;
}
