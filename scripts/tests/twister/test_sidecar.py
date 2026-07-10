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
    NetToolsSidecar,
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
    assert isinstance(SidecarImporter.get_sidecar('net-tools'), NetToolsSidecar)
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


# --- net-tools --------------------------------------------------------------

def test_net_tools_setup_skips_when_script_missing(tmp_path):
    instance = _make_instance(tmp_path)
    sidecar = NetToolsSidecar()
    sidecar.configure(instance)
    sidecar.net_setup = None

    with mock.patch("subprocess.run") as run_mock:
        proceed = sidecar.setup()

    assert proceed is False
    run_mock.assert_not_called()
    assert instance.status == TwisterStatus.SKIP


def test_net_tools_start_stop_commands(tmp_path):
    instance = _make_instance(tmp_path, {"net_iface": "zeth7"})
    os.makedirs(instance.build_dir, exist_ok=True)
    sidecar = NetToolsSidecar()
    sidecar.configure(instance)
    sidecar.net_setup = "/opt/net-tools/net-setup.sh"

    calls = []

    def _fake_run(cmd, **_kwargs):
        calls.append(cmd)
        return mock.Mock(returncode=0, stderr="", stdout="")

    with mock.patch("os.geteuid", return_value=0), \
         mock.patch("subprocess.run", side_effect=_fake_run):
        assert sidecar.setup() is True
        sidecar.teardown()

    # A stale-cleanup stop, then start, then the teardown stop, all on zeth7 with
    # the generated per-instance config.
    conf = os.path.join(instance.build_dir, "net-tools.conf")
    assert [c[-1] for c in calls] == ["stop", "start", "stop"]
    for c in calls:
        assert c == ["/opt/net-tools/net-setup.sh", "--iface", "zeth7",
                     "--config", conf, c[-1]]
    # The generated config puts the host on this instance's own subnet.
    with open(conf) as f:
        assert sidecar.host_ip in f.read()


def test_net_iface_and_addresses_are_unique_and_valid():
    from twisterlib.sidecar import get_net_addresses, get_net_iface_name
    iface = get_net_iface_name("/some/long/build/dir")
    assert iface.startswith("zeth") and len(iface) <= 15
    assert iface == get_net_iface_name("/some/long/build/dir")
    assert get_net_iface_name("/other") != iface

    host, guest, prefix = get_net_addresses("/some/long/build/dir")
    assert host.endswith(".2") and guest.endswith(".1") and prefix == 24
    assert host.rsplit(".", 1)[0] == guest.rsplit(".", 1)[0]
    assert get_net_addresses("/other")[0] != host


def test_net_shared_subnet_uses_fixed_addresses(tmp_path):
    from twisterlib.sidecar import NET_TOOLS_SHARED_SUBNET

    # Without the flag, each instance gets its own private subnet.
    (tmp_path / "a").mkdir()
    private = NetToolsSidecar()
    private.configure(_make_instance(tmp_path / "a"))
    assert private.host_ip.startswith("10.")

    # With it, the fixed 192.0.2.x addresses the companion expects are used.
    (tmp_path / "b").mkdir()
    shared = NetToolsSidecar()
    shared.configure(_make_instance(tmp_path / "b", {"net_shared_subnet": True}))
    assert (shared.host_ip, shared.guest_ip, shared.prefix) == NET_TOOLS_SHARED_SUBNET


def test_net_tools_uses_sudo_when_not_root(tmp_path):
    instance = _make_instance(tmp_path)
    sidecar = NetToolsSidecar()
    sidecar.configure(instance)
    sidecar.net_setup = "/opt/net-tools/net-setup.sh"

    with mock.patch("os.geteuid", return_value=1000):
        cmd = sidecar._net_setup_cmd("start")

    assert cmd[:2] == ["sudo", "-n"]
    assert cmd[-1] == "start"


def test_net_tools_launches_and_stops_companion(tmp_path):
    instance = _make_instance(tmp_path, {"net_iface": "zeth7",
                                         "net_tools_apps": ["echo-server"]})
    os.makedirs(instance.build_dir, exist_ok=True)
    sidecar = NetToolsSidecar()
    sidecar.configure(instance)
    sidecar.net_setup = "/opt/net-tools/net-setup.sh"

    app_proc = mock.Mock()

    with mock.patch("os.geteuid", return_value=0), \
         mock.patch("subprocess.run", return_value=mock.Mock(returncode=0, stderr="", stdout="")), \
         mock.patch("os.path.exists", return_value=True), \
         mock.patch("subprocess.Popen", return_value=app_proc) as popen_mock:
        assert sidecar.setup() is True
        # The known "echo-server" shortcut expands and binds to this iface.
        assert popen_mock.call_args.args[0] == ["/opt/net-tools/echo-server", "-i", "zeth7"]

    with mock.patch("twisterlib.sidecar.terminate_process") as term_mock:
        sidecar.teardown()

    term_mock.assert_called_once_with(app_proc)


def test_net_tools_skips_when_companion_missing(tmp_path):
    instance = _make_instance(tmp_path, {"net_tools_apps": ["echo-server"]})
    os.makedirs(instance.build_dir, exist_ok=True)
    sidecar = NetToolsSidecar()
    sidecar.configure(instance)
    sidecar.net_setup = "/opt/net-tools/net-setup.sh"

    with mock.patch("os.geteuid", return_value=0), \
         mock.patch("subprocess.run", return_value=mock.Mock(returncode=0, stderr="", stdout="")), \
         mock.patch("os.path.exists", return_value=False), \
         mock.patch("subprocess.Popen") as popen_mock:
        proceed = sidecar.setup()

    assert proceed is False
    popen_mock.assert_not_called()
    assert instance.status == TwisterStatus.SKIP


def test_net_harness_implies_net_tools_sidecar(tmp_path):
    # A `harness: net` test with no explicit sidecar gets net-tools attached.
    instance = _make_instance(tmp_path)
    instance.testsuite.harness = "net"
    instance.testsuite.sidecar = None
    reloaded = TestInstance(
        testsuite=instance.testsuite, platform=instance.platform,
        toolchain="zephyr", outdir=tmp_path / "out2",
    )
    assert reloaded.sidecar == "net-tools"

    # An explicit sidecar still wins.
    instance.testsuite.sidecar = "virtiofs"
    reloaded = TestInstance(
        testsuite=instance.testsuite, platform=instance.platform,
        toolchain="zephyr", outdir=tmp_path / "out3",
    )
    assert reloaded.sidecar == "virtiofs"
