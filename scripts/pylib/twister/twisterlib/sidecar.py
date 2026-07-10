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

import logging

from twisterlib.testinstance import TestInstance

logger = logging.getLogger('twister')


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


class SidecarImporter:
    _SIDECARS: dict = {}

    @staticmethod
    def get_sidecar(sidecar_name):
        if not sidecar_name:
            return None
        sidecar_class = SidecarImporter._SIDECARS.get(sidecar_name)
        if sidecar_class is None:
            logger.error(f"sidecar {sidecar_name} not implemented")
            return None
        return sidecar_class()
