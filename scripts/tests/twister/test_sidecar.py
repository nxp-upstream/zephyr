#!/usr/bin/env python3
# Copyright (c) 2025 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
"""Tests for the Sidecar classes of twister."""

import os
import struct
from unittest import mock

from twisterlib.sidecar import (
    IVSHMEM_COV_MAGIC,
    IvshmemSidecar,
    SidecarImporter,
    VirtiofsSidecar,
    get_ivshmem_backing_path,
    get_virtiofs_socket_path,
)
from twisterlib.statuses import TwisterStatus
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
    assert isinstance(SidecarImporter.get_sidecar('virtiofs'), VirtiofsSidecar)
    assert isinstance(SidecarImporter.get_sidecar('ivshmem'), IvshmemSidecar)
    assert SidecarImporter.get_sidecar(None) is None
    assert SidecarImporter.get_sidecar('') is None
    assert SidecarImporter.get_sidecar('nope') is None


# --- virtiofs ---------------------------------------------------------------

def test_get_virtiofs_socket_path_is_short_and_stable():
    long_build_dir = "/tmp/" + "a" * 300 + "/build"
    path = get_virtiofs_socket_path(long_build_dir)

    assert path == get_virtiofs_socket_path(long_build_dir)
    assert len(path) < 108
    assert get_virtiofs_socket_path("/other/build") != path


def test_virtiofs_setup_skips_when_virtiofsd_missing(tmp_path):
    instance = _make_instance(tmp_path)
    sidecar = VirtiofsSidecar()
    sidecar.configure(instance)
    sidecar.virtiofsd_bin = None

    with mock.patch("subprocess.Popen") as popen_mock:
        proceed = sidecar.setup()

    assert proceed is False
    popen_mock.assert_not_called()
    assert instance.status == TwisterStatus.SKIP


def test_virtiofs_setup_seeds_shared_dir_and_starts_daemon(tmp_path):
    template = tmp_path / "src" / "shared"
    (template / "sub").mkdir(parents=True)
    (template / "file").write_text("host content")

    instance = _make_instance(tmp_path, {"virtiofs_shared": "shared"})
    sidecar = VirtiofsSidecar()
    sidecar.configure(instance)
    sidecar.virtiofsd_bin = "/usr/bin/virtiofsd"

    with mock.patch("subprocess.Popen") as popen_mock:
        popen_mock.return_value.pid = 4321
        proceed = sidecar.setup()

    assert proceed is True
    assert os.path.isfile(os.path.join(sidecar.shared_dir, "file"))
    assert os.path.isdir(os.path.join(sidecar.shared_dir, "sub"))
    args = popen_mock.call_args.args[0]
    assert args[0] == "/usr/bin/virtiofsd"
    assert f"--socket-path={sidecar.socket_path}" in args
    assert sidecar.shared_dir in args


def test_virtiofs_teardown_terminates_daemon(tmp_path):
    instance = _make_instance(tmp_path)
    sidecar = VirtiofsSidecar()
    sidecar.configure(instance)
    proc = mock.Mock()
    sidecar._virtiofsd_proc = proc
    sidecar._virtiofsd_log = mock.Mock()
    open(sidecar.socket_path, "w").close()

    with mock.patch("twisterlib.sidecar.terminate_process") as term_mock:
        sidecar.teardown()

    term_mock.assert_called_once_with(proc)
    proc.wait.assert_called_once()
    assert sidecar._virtiofsd_proc is None
    assert not os.path.exists(sidecar.socket_path)


# --- ivshmem ----------------------------------------------------------------

def test_get_ivshmem_backing_path_is_unique_and_in_shm():
    path = get_ivshmem_backing_path("/some/long/build/dir")
    assert path.startswith("/dev/shm/")
    assert path == get_ivshmem_backing_path("/some/long/build/dir")
    assert get_ivshmem_backing_path("/other/build") != path


def _build_ivshmem_blob(files, magic=IVSHMEM_COV_MAGIC, version=1):
    body = b""
    for name, data in files:
        name_b = name.encode() + b"\x00"
        body += struct.pack("<2I", len(name_b), len(data)) + name_b + data
        while len(body) % 4:
            body += b"\x00"
    header = struct.pack("<4I", magic, version, len(files), 16 + len(body))
    return header + body


def test_ivshmem_teardown_extracts_gcda(tmp_path):
    instance = _make_instance(tmp_path)
    sidecar = IvshmemSidecar()
    sidecar.configure(instance)

    gcda_path = os.path.join(instance.build_dir, "sub", "foo.c.gcda")
    tagged_path = os.path.join(instance.build_dir, "sub", "foo.c.gcda@@suite.test")
    outside = os.path.join(str(tmp_path), "escape.gcda")
    blob = _build_ivshmem_blob([
        (gcda_path, b"GCDA-DATA"),
        (tagged_path, b"TAGGED-DATA"),
        (outside, b"NOPE"),
    ])

    backing = tmp_path / "backing"
    backing.write_bytes(blob)
    sidecar.ivshmem_backing = str(backing)

    sidecar.teardown()

    with open(gcda_path, "rb") as fp:
        assert fp.read() == b"GCDA-DATA"
    with open(tagged_path, "rb") as fp:
        assert fp.read() == b"TAGGED-DATA"
    assert not os.path.exists(outside)
    assert not os.path.exists(str(backing))


def test_ivshmem_teardown_ignores_missing_magic(tmp_path):
    instance = _make_instance(tmp_path)
    sidecar = IvshmemSidecar()
    sidecar.configure(instance)

    gcda_path = os.path.join(instance.build_dir, "foo.c.gcda")
    blob = _build_ivshmem_blob([(gcda_path, b"DATA")], magic=0xDEADBEEF)
    backing = tmp_path / "backing"
    backing.write_bytes(blob)
    sidecar.ivshmem_backing = str(backing)

    sidecar.teardown()

    assert not os.path.exists(gcda_path)
    assert not os.path.exists(str(backing))


