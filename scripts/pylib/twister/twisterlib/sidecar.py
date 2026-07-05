# Copyright (c) 2025 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
"""Sidecars: host-side resources provisioned around a test run.

A sidecar is selected with the ``sidecar:`` testsuite field and is orthogonal
to the ``harness:`` field: the harness processes the guest's output while the
sidecar provisions a host-side resource (a ``virtiofsd`` daemon, an ivshmem
shared memory region, ...) around the run. The runner calls :meth:`Sidecar.setup`
before the handler executes the test and :meth:`Sidecar.teardown` afterwards.

This keeps output processing and host provisioning independent, so any harness
(``console`` for a sample, ``ztest`` for a test) can be paired with any sidecar,
and twister can also attach a sidecar itself (for example to capture coverage
over ivshmem) without the test having to opt in.
"""
from __future__ import annotations

import glob
import hashlib
import logging
import os
import shlex
import shutil
import struct
import subprocess
import tempfile
import time

from twisterlib.environment import ZEPHYR_BASE
from twisterlib.handlers import terminate_process
from twisterlib.statuses import TwisterStatus
from twisterlib.testinstance import TestInstance

logger = logging.getLogger('twister')


def get_virtiofs_socket_path(build_dir: str) -> str:
    """Return a short, unique vhost-user socket path for a build directory.

    The virtiofs sidecar launches ``virtiofsd`` on a UNIX socket that QEMU
    connects to through a ``-chardev socket`` flag. AF_UNIX paths are limited
    to roughly 108 bytes, so the socket cannot live inside ``build_dir`` whose
    path is often too long. Derive a short unique name from a hash of
    ``build_dir`` and place it in the system temporary directory instead.

    Both the CMake configure step (which bakes the path into QEMU's flags) and
    the sidecar (which launches ``virtiofsd``) compute this independently from
    ``build_dir``, so they always agree without sharing any state.
    """
    digest = hashlib.sha1(os.path.abspath(build_dir).encode()).hexdigest()[:16]
    return os.path.join(tempfile.gettempdir(), f'twister-virtiofs-{digest}.sock')


def get_net_iface_name(build_dir: str) -> str:
    """Return a unique host tap interface name for a build directory.

    Linux interface names are limited to 15 characters (IFNAMSIZ - 1), so use a
    short hash of ``build_dir`` after the ``zeth`` prefix. Both the sidecar (which
    creates the tap) and the runner (which bakes CONFIG_ETH_QEMU_IFACE_NAME into
    the build so QEMU attaches to it) derive this independently from ``build_dir``.
    """
    digest = hashlib.sha1(os.path.abspath(build_dir).encode()).hexdigest()[:8]
    return f"zeth{digest}"


def get_net_addresses(build_dir: str) -> tuple[str, str, int]:
    """Return ``(host_ipv4, guest_ipv4, prefix_len)`` unique per build directory.

    Each instance gets its own /24 out of the 10.0.0.0/8 private range so that
    parallel net tests do not share a subnet on the host (which would make the
    host's reply routing ambiguous). The host is ``.2`` and the guest ``.1``.
    """
    digest = hashlib.sha1(os.path.abspath(build_dir).encode()).digest()
    octet_a, octet_b = digest[0], digest[1]
    return f"10.{octet_a}.{octet_b}.2", f"10.{octet_a}.{octet_b}.1", 24


# The fixed addresses net-tools uses by default (zeth.conf): host ``.2``,
# guest ``.1``. Used by tests whose companion hard-codes these; see
# ``net_shared_subnet``.
NET_TOOLS_SHARED_SUBNET = ("192.0.2.2", "192.0.2.1", 24)


def get_ivshmem_backing_path(build_dir: str) -> str:
    """Return a short, unique ivshmem backing file path for a build directory.

    QEMU backs the ivshmem-plain shared memory with a file
    (``memory-backend-file``). It lives in ``/dev/shm`` (tmpfs) so the sidecar
    can read it back cheaply, with a name derived from a hash of ``build_dir``
    so parallel instances do not collide. Both the CMake configure step (which
    bakes the path into QEMU's flags via the environment) and the sidecar
    compute it independently from ``build_dir``.
    """
    digest = hashlib.sha1(os.path.abspath(build_dir).encode()).hexdigest()[:16]
    return os.path.join('/dev/shm', f'twister-ivshmem-{digest}')


def get_ivshmem_socket_path(build_dir: str) -> str:
    """Return a short, unique ivshmem-server socket path for a build directory.

    The ivshmem-doorbell device connects to an ``ivshmem-server`` over a UNIX
    socket (QEMU ``-chardev socket``). AF_UNIX paths are limited to roughly 108
    bytes, so the socket lives in the system temporary directory with a name
    derived from a hash of ``build_dir`` so parallel instances do not collide.
    Both the CMake configure step (which bakes the path into QEMU's flags via
    the environment) and the sidecar (which launches the server) compute this
    independently from ``build_dir``.
    """
    digest = hashlib.sha1(os.path.abspath(build_dir).encode()).hexdigest()[:16]
    return os.path.join(tempfile.gettempdir(), f'twister-ivshmem-server-{digest}.sock')


class Sidecar:
    """Base class for host-side resources provisioned around a test run."""

    def __init__(self):
        self.instance: TestInstance | None = None

    def configure(self, instance: TestInstance):
        self.instance = instance

    def setup(self) -> bool:
        """Provision the resource before the handler runs the test.

        Returns True when the handler should proceed, False to skip execution
        (for example when a required host tool is missing); :meth:`teardown` is
        only called when setup returned True.
        """
        return True

    def teardown(self) -> None:
        """Release the resource after the handler returns.

        Called in a ``finally`` block, so it must be safe to call even if the
        test failed or timed out.
        """


class VirtiofsSidecar(Sidecar):
    """Shares a host directory with the guest over virtiofs.

    Tests using this sidecar run on QEMU with a ``vhost-user-fs-pci`` device.
    That device needs a ``virtiofsd`` daemon running on the host and connected
    to QEMU through a UNIX socket before QEMU boots. The sidecar starts the
    daemon in :meth:`setup` and stops it in :meth:`teardown`.

    The socket path is derived from the build directory (see
    :func:`get_virtiofs_socket_path`) and injected into QEMU's flags by the
    runner at CMake configure time, so many tests can run in parallel without
    colliding on a shared socket.

    Supported ``harness_config`` keys:

    - ``virtiofs_shared``: path, relative to the test source directory, of a
      template directory whose contents seed the shared filesystem. A private
      writable copy is made under the build directory so tests may modify it.
    - ``virtiofsd_bin``: explicit path to the ``virtiofsd`` binary. Defaults to
      the first ``virtiofsd`` found on ``PATH`` or in a common libexec location.
    - ``virtiofs_extra_args``: list of additional arguments passed to
      ``virtiofsd``.
    """

    # Common locations for the Rust virtiofsd, which is frequently installed
    # outside PATH (e.g. as a QEMU helper) by distribution packages.
    VIRTIOFSD_FALLBACK_PATHS = (
        '/usr/libexec/virtiofsd',
        '/usr/lib/qemu/virtiofsd',
        '/usr/local/libexec/virtiofsd',
    )

    @classmethod
    def find_virtiofsd(cls):
        found = shutil.which('virtiofsd')
        if found:
            return found
        for candidate in cls.VIRTIOFSD_FALLBACK_PATHS:
            if os.access(candidate, os.X_OK):
                return candidate
        return None

    def configure(self, instance: TestInstance):
        super().configure(instance)
        config = instance.testsuite.harness_config or {}
        self.virtiofsd_bin = config.get('virtiofsd_bin') or self.find_virtiofsd()
        self.virtiofs_shared = config.get('virtiofs_shared')
        self.virtiofs_extra_args = config.get('virtiofs_extra_args', [])
        self.socket_path = get_virtiofs_socket_path(instance.build_dir)
        self.shared_dir = os.path.join(instance.build_dir, 'virtiofs-shared')
        self._virtiofsd_proc = None
        self._virtiofsd_log = None

    def setup(self) -> bool:
        if not self.virtiofsd_bin:
            self.instance.status = TwisterStatus.SKIP
            self.instance.reason = "virtiofsd not found on host"
            self.instance.add_missing_case_status(TwisterStatus.SKIP, self.instance.reason)
            logger.warning(f"SIDECAR:{self.__class__.__name__}: virtiofsd not found,"
                           f" skipping {self.instance.name}")
            return False

        # Seed a private, writable copy of the shared directory so tests that
        # create or modify files do not mutate the checked-in template.
        self._remove_shared_dir()
        if self.virtiofs_shared:
            template = os.path.join(
                self.instance.testsuite.source_dir, self.virtiofs_shared
            )
            shutil.copytree(template, self.shared_dir)
        else:
            os.makedirs(self.shared_dir, exist_ok=True)

        # A stale socket from a previous run would make virtiofsd refuse to bind.
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)

        command = [
            self.virtiofsd_bin,
            f'--socket-path={self.socket_path}',
            '--shared-dir', self.shared_dir,
        ]
        command.extend(self.virtiofs_extra_args)

        log_path = os.path.join(self.instance.build_dir, 'virtiofsd.log')
        try:
            # The daemon outlives setup(); the handle is closed in teardown().
            self._virtiofsd_log = open(log_path, 'w')  # noqa: SIM115
            self._virtiofsd_proc = subprocess.Popen(
                command,
                stdout=self._virtiofsd_log,
                stderr=subprocess.STDOUT,
            )
        except OSError as err:
            self.instance.status = TwisterStatus.ERROR
            self.instance.reason = f"could not launch virtiofsd: {err}"
            self.instance.add_missing_case_status(TwisterStatus.ERROR, self.instance.reason)
            logger.error(f"SIDECAR:{self.__class__.__name__}: {self.instance.reason}")
            return False

        logger.debug(f"SIDECAR:{self.__class__.__name__}: started virtiofsd"
                     f" (pid {self._virtiofsd_proc.pid}) on {self.socket_path}")
        return True

    def teardown(self) -> None:
        if self._virtiofsd_proc is not None:
            terminate_process(self._virtiofsd_proc)
            try:
                self._virtiofsd_proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._virtiofsd_proc.kill()
            self._virtiofsd_proc = None
        if self._virtiofsd_log is not None:
            self._virtiofsd_log.close()
            self._virtiofsd_log = None
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)
        # Tests may create host directories with restrictive permissions, which
        # would make a later shutil.rmtree (here or when twister clobbers the
        # output) fail. Remove the share tolerantly now.
        self._remove_shared_dir()

    def _remove_shared_dir(self):
        if not os.path.exists(self.shared_dir):
            return

        def _on_error(func, path, _exc):
            # A directory the guest created without write/exec permission cannot
            # be traversed or unlinked; restore access and retry.
            os.chmod(os.path.dirname(path), 0o700)
            os.chmod(path, 0o700)
            func(path)

        shutil.rmtree(self.shared_dir, onexc=_on_error)


# Layout written by subsys/testsuite/coverage/coverage.c
# (gcov_coverage_dump_tagged). Keep in sync with the C definitions there.
IVSHMEM_COV_MAGIC = 0x564F435A  # "ZCOV"
_IVSHMEM_COV_HEADER = struct.Struct('<4I')  # magic, version, count, used
_IVSHMEM_COV_RECORD = struct.Struct('<2I')  # name_len, data_len

# Layout written by subsys/tracing/tracing_backend_ivshmem.c. Keep in sync.
IVSHMEM_TRACE_MAGIC = 0x4352545A  # "ZTRC"
_IVSHMEM_TRACE_HEADER = struct.Struct('<4I')  # magic, version, used, overflow


class IvshmemSidecar(Sidecar):
    """Gives the guest a host-backed ivshmem shared memory region.

    QEMU backs the ivshmem-plain device with a file in ``/dev/shm``; the guest
    reaches it through the ``qemu,ivshmem`` PCI device. The path is per-build-dir
    (see :func:`get_ivshmem_backing_path`) and injected into QEMU's flags by the
    runner at CMake configure time, so instances run in parallel without sharing
    a region.

    :meth:`teardown` reads the region back and dispatches on the magic in its
    first word, so one sidecar serves several "guest writes a blob, host reads
    it" uses without the test picking a different sidecar:

    - Coverage (``CONFIG_COVERAGE_IVSHMEM``): the guest writes gcov data into the
      region on exit and the sidecar extracts the ``.gcda`` files into the build
      directory where gcovr expects them (mirroring the semihosting flow, so no
      serial-log parsing is needed). Per-test dumps are carried as
      ``<gcda>@@<suite>.<test>`` records.
    - Tracing (``CONFIG_TRACING_BACKEND_IVSHMEM``): the guest streams its CTF
      trace into the region and the sidecar writes it out as a CTF trace
      directory (stream plus metadata) under ``ctf/`` in the build directory.
    """

    def configure(self, instance: TestInstance):
        super().configure(instance)
        self.ivshmem_backing = get_ivshmem_backing_path(instance.build_dir)

    def setup(self) -> bool:
        # Remove any stale backing file so QEMU creates a fresh, zeroed region.
        if os.path.exists(self.ivshmem_backing):
            os.unlink(self.ivshmem_backing)
        return True

    def teardown(self) -> None:
        try:
            self._extract()
        finally:
            if os.path.exists(self.ivshmem_backing):
                os.unlink(self.ivshmem_backing)

    def _extract(self) -> None:
        # The region carries whatever the guest wrote; dispatch on the magic in
        # its first word so the same sidecar serves coverage and tracing.
        if not os.path.exists(self.ivshmem_backing):
            return

        with open(self.ivshmem_backing, 'rb') as fp:
            blob = fp.read()

        if len(blob) < 4:
            return

        magic = struct.unpack_from('<I', blob, 0)[0]
        if magic == IVSHMEM_COV_MAGIC:
            self._extract_coverage(blob)
        elif magic == IVSHMEM_TRACE_MAGIC:
            self._extract_trace(blob)

    def _extract_coverage(self, blob) -> None:
        if len(blob) < _IVSHMEM_COV_HEADER.size:
            return

        magic, version, count, used = _IVSHMEM_COV_HEADER.unpack_from(blob, 0)
        if magic != IVSHMEM_COV_MAGIC:
            # No (complete) coverage dump was produced; nothing to do.
            return
        if version != 1:
            logger.warning(f"SIDECAR:{self.__class__.__name__}: unsupported ivshmem"
                           f" coverage version {version}")
            return

        build_dir = os.path.realpath(self.instance.build_dir)
        off = _IVSHMEM_COV_HEADER.size
        extracted = 0
        for _ in range(count):
            if off + _IVSHMEM_COV_RECORD.size > used:
                break
            name_len, data_len = _IVSHMEM_COV_RECORD.unpack_from(blob, off)
            off += _IVSHMEM_COV_RECORD.size
            name = blob[off:off + name_len].split(b'\x00', 1)[0].decode(errors='replace')
            data = blob[off + name_len:off + name_len + data_len]
            off += name_len + data_len
            off = (off + 3) & ~3  # 4 byte alignment

            # The guest sends the absolute build path of each .gcda file (per-test
            # dumps append "@@<tag>"); only write files that stay within this
            # instance's build directory.
            dest = os.path.realpath(name)
            if os.path.commonpath([build_dir, dest]) != build_dir:
                logger.warning(f"SIDECAR:{self.__class__.__name__}: ignoring gcda"
                               f" outside build dir: {name}")
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, 'wb') as gcda:
                gcda.write(data)
            extracted += 1

        logger.debug(f"SIDECAR:{self.__class__.__name__}: extracted {extracted} gcda"
                     f" file(s) from {self.ivshmem_backing}")

    def _extract_trace(self, blob) -> None:
        if len(blob) < _IVSHMEM_TRACE_HEADER.size:
            return

        magic, version, used, overflow = _IVSHMEM_TRACE_HEADER.unpack_from(blob, 0)
        if magic != IVSHMEM_TRACE_MAGIC:
            return
        if version != 1:
            logger.warning(f"SIDECAR:{self.__class__.__name__}: unsupported ivshmem"
                           f" trace version {version}")
            return

        used = min(used, len(blob))
        stream = blob[_IVSHMEM_TRACE_HEADER.size:used]

        # Lay out a CTF trace directory (stream + metadata) that babeltrace2 or
        # scripts/tracing/parse_ctf.py can decode directly.
        ctf_dir = os.path.join(self.instance.build_dir, 'ctf')
        os.makedirs(ctf_dir, exist_ok=True)
        with open(os.path.join(ctf_dir, 'channel0_0'), 'wb') as out:
            out.write(stream)
        metadata = os.path.join(ZEPHYR_BASE, 'subsys', 'tracing', 'ctf', 'tsdl', 'metadata')
        if os.path.exists(metadata):
            shutil.copyfile(metadata, os.path.join(ctf_dir, 'metadata'))

        if overflow:
            logger.warning(f"SIDECAR:{self.__class__.__name__}: ivshmem trace region"
                           f" filled up, trailing trace data was dropped")
        logger.debug(f"SIDECAR:{self.__class__.__name__}: extracted {len(stream)} byte(s)"
                     f" of CTF trace into {ctf_dir}")


class IvshmemServerSidecar(Sidecar):
    """Run an ``ivshmem-server`` for a guest using the ivshmem-doorbell device.

    Unlike ivshmem-plain (a shared memory file), the doorbell variant connects
    QEMU to an ``ivshmem-server`` over a UNIX socket; the server owns the shared
    memory and relays inter-peer interrupts (doorbells). The server must be
    running before QEMU boots. The sidecar starts it in :meth:`setup` and stops
    it in :meth:`teardown`.

    The socket path is derived from the build directory (see
    :func:`get_ivshmem_socket_path`) and injected into QEMU's flags by the runner
    at CMake configure time, so many tests can run in parallel without colliding.

    Supported ``harness_config`` keys:

    - ``ivshmem_server_bin``: explicit path to ``ivshmem-server``. Defaults to
      the first one found on ``PATH`` or in the Zephyr SDK host tools.
    - ``ivshmem_server_size``: shared memory size in bytes (default 4194304).
      qemu_x86_64 has little RAM, so tests there pass a smaller value.
    - ``ivshmem_server_vectors``: number of interrupt vectors (default 2, to
      match the CONFIG_IVSHMEM_MSI_X_VECTORS default).
    """

    # Known Zephyr SDK host-tools layouts, newest first: SDK >= 0.16 nests them
    # under hosttools/, older ones put sysroots/ at the top. The sysroot
    # directory name (e.g. x86_64-pokysdk-linux) is globbed.
    SDK_IVSHMEM_SERVER_GLOBS = (
        os.path.join('hosttools', 'sysroots', '*', 'usr', 'bin', 'ivshmem-server'),
        os.path.join('sysroots', '*', 'usr', 'bin', 'ivshmem-server'),
    )

    @classmethod
    def find_ivshmem_server(cls):
        found = shutil.which('ivshmem-server')
        if found:
            return found
        # The binary ships with the Zephyr SDK host tools but is usually not on
        # PATH; look under the configured SDK at its known, fixed locations
        # (a full os.walk of the SDK would be needlessly slow).
        sdk = os.environ.get('ZEPHYR_SDK_INSTALL_DIR')
        if sdk:
            for pattern in cls.SDK_IVSHMEM_SERVER_GLOBS:
                for candidate in glob.glob(os.path.join(sdk, pattern)):
                    if os.access(candidate, os.X_OK):
                        return candidate
        return None

    def configure(self, instance: TestInstance):
        super().configure(instance)
        config = instance.testsuite.harness_config or {}
        self.server_bin = config.get('ivshmem_server_bin') or self.find_ivshmem_server()
        self.size = int(config.get('ivshmem_server_size', 4194304))
        self.vectors = int(config.get('ivshmem_server_vectors', 2))
        self.socket_path = get_ivshmem_socket_path(instance.build_dir)
        # ivshmem-server -M names a POSIX shared memory object under /dev/shm.
        digest = hashlib.sha1(os.path.abspath(instance.build_dir).encode()).hexdigest()[:16]
        self.shm_name = f'twister-ivshmem-server-{digest}'
        self.shm_path = os.path.join('/dev/shm', self.shm_name)
        self._proc = None
        self._log = None

    def setup(self) -> bool:
        if not self.server_bin:
            self.instance.status = TwisterStatus.SKIP
            self.instance.reason = "ivshmem-server not found on host"
            self.instance.add_missing_case_status(TwisterStatus.SKIP, self.instance.reason)
            logger.warning(f"SIDECAR:{self.__class__.__name__}: ivshmem-server not found,"
                           f" skipping {self.instance.name}")
            return False

        # Clear any stale socket/shm left by a previous, crashed run.
        for stale in (self.socket_path, self.shm_path):
            if os.path.exists(stale):
                os.unlink(stale)

        command = [
            self.server_bin,
            '-F',  # foreground, so the sidecar owns the process
            '-S', self.socket_path,
            '-M', self.shm_name,
            '-l', str(self.size),
            '-n', str(self.vectors),
        ]

        log_path = os.path.join(self.instance.build_dir, 'ivshmem-server.log')
        try:
            # The server outlives setup(); the handle is closed in teardown().
            self._log = open(log_path, 'w')  # noqa: SIM115
            self._proc = subprocess.Popen(command, stdout=self._log, stderr=subprocess.STDOUT)
        except OSError as err:
            self.instance.status = TwisterStatus.ERROR
            self.instance.reason = f"could not launch ivshmem-server: {err}"
            self.instance.add_missing_case_status(TwisterStatus.ERROR, self.instance.reason)
            logger.error(f"SIDECAR:{self.__class__.__name__}: {self.instance.reason}")
            return False

        # QEMU refuses to start if the server socket is not yet listening, so
        # wait briefly for it to appear.
        for _ in range(50):
            if os.path.exists(self.socket_path):
                break
            if self._proc.poll() is not None:
                self.instance.status = TwisterStatus.ERROR
                self.instance.reason = "ivshmem-server exited during startup"
                self.instance.add_missing_case_status(TwisterStatus.ERROR, self.instance.reason)
                logger.error(f"SIDECAR:{self.__class__.__name__}: {self.instance.reason}")
                return False
            time.sleep(0.1)

        logger.debug(f"SIDECAR:{self.__class__.__name__}: started ivshmem-server"
                     f" (pid {self._proc.pid}) on {self.socket_path}")
        return True

    def teardown(self) -> None:
        if self._proc is not None:
            terminate_process(self._proc)
            try:
                self._proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._proc.kill()
            self._proc = None
        if self._log is not None:
            self._log.close()
            self._log = None
        for path in (self.socket_path, self.shm_path):
            if os.path.exists(path):
                os.unlink(path)


class NetToolsSidecar(Sidecar):
    """Bring up the host network environment a QEMU net test needs.

    Zephyr's net-tools ``net-setup.sh`` creates a ``tap`` interface that a guest
    built with ``CONFIG_NET_QEMU_ETHERNET`` attaches its NIC to (``-netdev tap,
    ifname=<iface>,script=no``). QEMU cannot open the tap unless it already
    exists, so the interface must be up before the guest boots and torn down
    afterwards - exactly a sidecar's :meth:`setup` / :meth:`teardown`.

    To run in parallel, each instance gets a unique interface name and its own
    /24 (see :func:`get_net_iface_name` / :func:`get_net_addresses`); the sidecar
    writes a matching net-setup.sh config for the host side and the runner bakes
    ``CONFIG_ETH_QEMU_IFACE_NAME`` and the guest/peer IPv4 addresses into the
    build so both ends agree. A test that manages its own addressing can override
    with ``net_iface`` / ``net_config`` and the runner then leaves the IPs alone.

    Creating a tap needs root; the sidecar runs ``net-setup.sh`` through
    ``sudo -n`` when not already root and skips the test (rather than hanging on
    a password prompt) when that is not available, or when net-tools is not
    found.

    Companion host apps (a net-tools echo-server, dnsmasq, ...) can be started
    alongside the interface with the ``net_tools_apps`` key so that functional
    tests have something to talk to. Each app is a command run after the
    interface is up and stopped before it is torn down; the tokens ``{net_tools}``,
    ``{iface}``, ``{host_ip}`` and ``{guest_ip}`` are substituted, and bare known
    names expand to a default command (see :data:`KNOWN_APPS`). Apps bind to this
    instance's interface, so they stay isolated across parallel runs.

    Supported ``harness_config`` keys:

    - ``net_iface``: interface name to create (default: derived per build dir).
    - ``net_config``: explicit net-setup.sh config file; disables the auto subnet.
    - ``net_setup_script``: explicit path to ``net-setup.sh``.
    - ``net_tools_apps``: list of companion host apps/commands to run.
    """

    # net-tools is a sibling of the zephyr repo in a west workspace.
    NET_TOOLS_FALLBACK_PATHS = (
        os.path.join(ZEPHYR_BASE, '..', 'tools', 'net-tools'),
        os.path.join(ZEPHYR_BASE, '..', 'net-tools'),
    )

    # Shortcuts: a bare app name expands to this command template. Placeholders:
    # {net_tools} (net-tools dir), {iface}, {host_ip}, {guest_ip}.
    KNOWN_APPS = {
        # Passive server for guest echo clients; binds to the iface (parallel-safe).
        'echo-server': '{net_tools}/echo-server -i {iface}',
        # Active client that drives a guest echo server; -e keeps retrying so it
        # tolerates the guest still booting, -i binds it to this instance's iface.
        'echo-client': '{net_tools}/echo-client -i {iface} -e {guest_ip}',
        # Stdlib HTTP server on port 8000 for guest HTTP clients. It binds all
        # interfaces on a fixed port, so only one may run at a time.
        'http-server': 'python3 {net_tools}/http-server.py',
        # Echo websocket server on port 9001. It hard-codes the 192.0.2.2
        # address, so pair it with net_shared_subnet.
        'websocket-server': 'python3 {net_tools}/zephyr-websocket-server.py',
    }

    @classmethod
    def find_net_setup(cls):
        base = os.environ.get('NET_TOOLS_BASE')
        candidates = [os.path.join(base, 'net-setup.sh')] if base else []
        candidates += [os.path.join(p, 'net-setup.sh') for p in cls.NET_TOOLS_FALLBACK_PATHS]
        for candidate in candidates:
            if os.path.exists(candidate):
                return os.path.abspath(candidate)
        return shutil.which('net-setup.sh')

    def configure(self, instance: TestInstance):
        super().configure(instance)
        config = instance.testsuite.harness_config or {}
        self.net_setup = config.get('net_setup_script') or self.find_net_setup()
        self.iface = config.get('net_iface') or get_net_iface_name(instance.build_dir)
        if config.get('net_shared_subnet'):
            # Some companions (e.g. the net-tools websocket server) hard-code the
            # 192.0.2.0/24 addresses. Use them instead of a private per-instance
            # subnet; the interface is still unique, but the shared addresses
            # mean only one such test may run at a time.
            self.host_ip, self.guest_ip, self.prefix = NET_TOOLS_SHARED_SUBNET
        else:
            self.host_ip, self.guest_ip, self.prefix = get_net_addresses(instance.build_dir)
        # An explicit config disables the auto per-instance subnet.
        self.net_config = config.get('net_config')
        self.net_tools_apps = config.get('net_tools_apps', [])
        self._started = False
        self._apps = []

    def _net_setup_cmd(self, action):
        cmd = []
        if os.geteuid() != 0:
            # -n so a missing passwordless sudo fails fast instead of prompting.
            cmd += ['sudo', '-n']
        cmd += [self.net_setup, '--iface', self.iface]
        if self.net_config:
            cmd += ['--config', self.net_config]
        cmd.append(action)
        return cmd

    def setup(self) -> bool:
        if not self.net_setup:
            self._skip("net-tools net-setup.sh not found "
                       "(set NET_TOOLS_BASE or install net-tools)")
            return False

        if os.geteuid() != 0 and subprocess.run(
            ['sudo', '-n', 'true'], capture_output=True
        ).returncode != 0:
            self._skip("creating a tap interface needs root or passwordless sudo")
            return False

        # Without an explicit config, write a per-instance one that puts the host
        # on this instance's own subnet (matching the guest IPs the runner bakes
        # in), so parallel tests do not share addresses.
        if not self.net_config:
            self.net_config = os.path.join(self.instance.build_dir, 'net-tools.conf')
            with open(self.net_config, 'w') as f:
                # Adding the address installs the connected /prefix route.
                # arp_accept lets a passive guest server become reachable: it
                # announces its address via gratuitous ARP (RFC 5227) on boot,
                # and this makes the host cache that MAC without needing the
                # guest to answer a broadcast ARP request.
                f.write(
                    'INTERFACE="$1"\n'
                    'ip link set dev "$INTERFACE" up\n'
                    f'ip address add {self.host_ip}/{self.prefix} dev "$INTERFACE"\n'
                    'sysctl -q -w "net.ipv4.conf.$INTERFACE.arp_accept=1"\n'
                )

        # Remove a stale interface left by a previous, crashed run.
        subprocess.run(self._net_setup_cmd('stop'), capture_output=True)

        result = subprocess.run(self._net_setup_cmd('start'), capture_output=True, text=True)
        if result.returncode != 0:
            self.instance.status = TwisterStatus.ERROR
            self.instance.reason = f"net-setup.sh failed: {result.stderr.strip()}"
            self.instance.add_missing_case_status(TwisterStatus.ERROR, self.instance.reason)
            logger.error(f"SIDECAR:{self.__class__.__name__}: {self.instance.reason}")
            return False

        self._started = True
        logger.debug(f"SIDECAR:{self.__class__.__name__}: brought up {self.iface}")

        return self._start_apps()

    def _start_apps(self) -> bool:
        net_tools = os.path.dirname(self.net_setup)
        for spec in self.net_tools_apps:
            command = self.KNOWN_APPS.get(spec, spec).format(
                net_tools=net_tools, iface=self.iface,
                host_ip=self.host_ip, guest_ip=self.guest_ip,
            )
            argv = shlex.split(command)
            binary = argv[0] if os.path.sep in argv[0] else shutil.which(argv[0])
            if not binary or not os.path.exists(binary):
                self._skip(f"companion app not found: {argv[0]} (build net-tools?)")
                return False

            # For interpreted apps (e.g. "python3 .../http-server.py") the binary
            # is the interpreter, so also confirm any net-tools file it runs exists.
            missing = next((tok for tok in argv[1:]
                            if net_tools in tok and not os.path.exists(tok)), None)
            if missing:
                self._skip(f"companion app not found: {missing} (build net-tools?)")
                return False

            name = os.path.basename(argv[0])
            log = open(os.path.join(self.instance.build_dir,  # noqa: SIM115
                                    f"net-tools-{name}.log"), 'w')
            # Own session so teardown can signal the whole process group.
            proc = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            self._apps.append((proc, log))
            logger.debug(f"SIDECAR:{self.__class__.__name__}: started {name}"
                         f" (pid {proc.pid})")
        return True

    def teardown(self) -> None:
        for proc, log in self._apps:
            terminate_process(proc)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
            log.close()
        self._apps = []

        if self._started:
            subprocess.run(self._net_setup_cmd('stop'), capture_output=True)
            self._started = False

    def _skip(self, reason):
        self.instance.status = TwisterStatus.SKIP
        self.instance.reason = reason
        self.instance.add_missing_case_status(TwisterStatus.SKIP, reason)
        logger.warning(f"SIDECAR:{self.__class__.__name__}: {reason},"
                       f" skipping {self.instance.name}")


class SidecarImporter:
    _SIDECARS = {
        'virtiofs': VirtiofsSidecar,
        'ivshmem': IvshmemSidecar,
        'ivshmem-server': IvshmemServerSidecar,
        'net-tools': NetToolsSidecar,
    }

    @staticmethod
    def get_sidecar(sidecar_name):
        if not sidecar_name:
            return None
        sidecar_class = SidecarImporter._SIDECARS.get(sidecar_name)
        if sidecar_class is None:
            logger.error(f"sidecar {sidecar_name} not implemented")
            return None
        return sidecar_class()
