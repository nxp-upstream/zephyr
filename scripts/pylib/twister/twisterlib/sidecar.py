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

import hashlib
import logging
import os
import shutil
import subprocess
import tempfile

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


class SidecarImporter:
    _SIDECARS = {
        'virtiofs': VirtiofsSidecar,
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
