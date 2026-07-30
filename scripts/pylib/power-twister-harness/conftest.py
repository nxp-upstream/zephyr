# Copyright: (c)  2025, Intel Corporation
# Copyright 2025 NXP
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import json
import logging
from typing import TYPE_CHECKING

import pytest
from twister_harness import DeviceAdapter

if TYPE_CHECKING:
    from abstract.PowerMonitor import PowerMonitor


def pytest_addoption(parser):
    parser.addoption('--testdata')


def _get_pm_probe_fixture_value(dut: DeviceAdapter) -> str | None:
    for fixture in dut.device_config.fixtures or []:
        if fixture.startswith('pm_probe:'):
            return fixture.split(':', 1)[1]
    return None


@pytest.fixture
def probe_class(dut: DeviceAdapter) -> 'PowerMonitor':
    probe_path = _get_pm_probe_fixture_value(dut)
    if not probe_path:
        pytest.skip('pm_probe fixture not found for stm_powershield')

    from stm32l562e_dk.PowerShield import PowerShield

    probe = PowerShield()
    probe.connect(probe_path)
    probe.init()

    try:
        yield probe
    finally:
        probe.disconnect()


@pytest.fixture(name='test_data', scope='session')
def fixture_test_data(request: pytest.FixtureRequest) -> dict:
    measurements = request.config.getoption('--testdata')
    if not measurements:
        pytest.fail('--testdata must be provided')

    measurements = measurements.replace("'", '"')
    measurements_dict = json.loads(measurements)

    required_keys = [
        'elements_to_trim',
        'min_peak_distance',
        'min_peak_height',
        'peak_padding',
        'measurement_duration',
        'num_of_transitions',
        'expected_rms_values',
        'tolerance_percentage',
    ]

    for key in required_keys:
        if key not in measurements_dict:
            logging.error('Missing required test data key: %s', key)
            pytest.fail(f'Missing required test data key: {key}')

    return measurements_dict
