# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

import re
import asyncio
import logging
import sys

from enum import Enum
from bumble.core import BT_BR_EDR_TRANSPORT, ProtocolError
from bumble.pairing import PairingConfig, PairingDelegate
from bumble.l2cap import (
    ClassicChannelSpec,
)
from bumble.device import Device, DeviceConfiguration
from bumble.hci import Address, HCI_Write_Page_Timeout_Command,HCI_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES_ERROR
from bumble.snoop import BtSnooper
from bumble.transport import open_transport_or_link
from twister_harness import DeviceAdapter, Shell

logger = logging.getLogger(__name__)

L2CAP_SERVER_PSM=0x1001
Mode="base"
L2CAP_CHAN_IUT_ID=0
MAX_MTU=0xffff #65535
MIN_MTU=0x30 #48
STRESS_TEST_MAX_COUNT=50

class Delegate(PairingDelegate):
    def __init__(
        self,
        dut,
        io_capability,
    ):
        super().__init__(
            io_capability,
        )
        self.dut = dut

async def device_power_on(device) -> None:
    while True:
        try:
            await device.power_on()
            break
        except Exception:
            continue

async def wait_for_shell_response(dut, message):
    found = False
    lines = []
    try:
        while found is False:
            read_lines = dut.readlines()
            for line in read_lines:
                if message in line:
                    found = True
                    break
            lines = lines + read_lines
            await asyncio.sleep(0.1)
    except Exception as e:
        logger.error(f'{e}!', exc_info=True)
        raise e
    return found, lines

async def send_cmd_to_iut(shell, dut, cmd, parse=None, wait = True):
    found = False
    lines = shell.exec_command(cmd)
    if  wait:
        if parse is not None:
            found, lines = await wait_for_shell_response(dut, parse)
        else:
            found = True
    else:
        found = False
        if parse!=None:
            for line in lines:
                if parse in line:
                    found = True
                    break
        else:
            found = True
    logger.info(f'{lines}')
    assert found is True
    return lines

async def bumble_acl_connect(shell, dut, device, target_address):
    connection = None
    try:
        connection = await device.connect(target_address, transport=BT_BR_EDR_TRANSPORT)
        logger.info(f'=== Connected to {connection.peer_address}!')
    except Exception as e:
        logger.error(f'Fail to connect to {target_address}!')
        raise e
    return connection

async def bumble_acl_disconnect(shell, dut, device, connection, iut_id=L2CAP_CHAN_IUT_ID):
    await device.disconnect(connection, reason=HCI_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES_ERROR)
    found, lines = await wait_for_shell_response(dut, "Disconnected:")
    logger.info(f'lines : {lines}')
    assert found is True
    return found, lines

async def bumble_l2cap_disconnect(shell, dut, incoming_channel, iut_id=L2CAP_CHAN_IUT_ID):
    try:
        await incoming_channel.disconnect()
    except Exception as e:
        logger.error(f'Fail to send l2cap disconnect command!')
        raise e
    assert incoming_channel.disconnection_result is None

    found, lines = await wait_for_shell_response(dut, f"Channel {iut_id} disconnected")
    assert found is True