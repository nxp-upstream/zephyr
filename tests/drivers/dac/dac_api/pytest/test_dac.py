# SPDX-FileCopyrightText: Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
import re

from twister_harness import DeviceAdapter

SUCCESS_PATTERN = r"PROJECT EXECUTION SUCCESSFUL"
ZTEST_OUTPUT_PATTERN = r"test_task_write_value|Running TESTSUITE dac|START - test_task_write_value"


def _contains(lines: list[str], pattern: str) -> bool:
    regex = re.compile(pattern)
    return any(regex.search(line) for line in lines)


def test_dac_aux_output(dut: DeviceAdapter):
    aux_lines = dut.readlines_until(
        connection_index=1,
        regex=r"PROJECT EXECUTION SUCCESSFUL|PROJECT EXECUTION FAILED",
        timeout=30.0,
    )

    assert _contains(aux_lines, SUCCESS_PATTERN), (
        "DAC test did not report success on AUX UART.\n" + "\n".join(aux_lines)
    )

    assert _contains(aux_lines, ZTEST_OUTPUT_PATTERN), (
        "Did not detect expected DAC ztest output on AUX UART.\n" + "\n".join(aux_lines)
    )
