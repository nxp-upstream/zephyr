#!/usr/bin/env python3
# Copyright (c) 2025 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
"""Tests for the Sidecar classes of twister."""

import os
from unittest import mock

from twisterlib.sidecar import (
    SidecarImporter,
)
from twisterlib.testinstance import TestInstance


def _make_instance(tmp_path, harness_config=None):
    mock_platform = mock.Mock()
    mock_platform.name = "qemu_x86_64"
    mock_platform.normalized_name = "qemu_x86_64"

    mock_testsuite = mock.Mock(id="id", testcases=[])
    mock_testsuite.name = "mock_testsuite"
    mock_testsuite.harness_config = harness_config or {}
    mock_testsuite.source_dir = str(tmp_path / "src")
    os.makedirs(mock_testsuite.source_dir, exist_ok=True)

    outdir = tmp_path / "out"
    outdir.mkdir()

    return TestInstance(
        testsuite=mock_testsuite, platform=mock_platform, toolchain='zephyr', outdir=outdir
    )


def test_sidecar_importer_resolves_names():
    assert SidecarImporter.get_sidecar(None) is None
    assert SidecarImporter.get_sidecar('') is None
    assert SidecarImporter.get_sidecar('nope') is None
