# Copyright 2024 NXP
#
# SPDX-License-Identifier: Apache-2.0

import asyncio
import logging
import sys
import re
from bumble.core import *
from bumble.l2cap import *
from bumble.hci import *

from bumble.core import (
    BT_ADVANCED_AUDIO_DISTRIBUTION_SERVICE,
    BT_AUDIO_SINK_SERVICE,
    BT_AUDIO_SOURCE_SERVICE,
    BT_AV_REMOTE_CONTROL_CONTROLLER_SERVICE,
    BT_AV_REMOTE_CONTROL_SERVICE,
    BT_AV_REMOTE_CONTROL_TARGET_SERVICE,
    BT_AVCTP_PROTOCOL_ID,
    BT_AVDTP_PROTOCOL_ID,
    BT_BR_EDR_TRANSPORT,
    BT_GENERIC_AUDIO_SERVICE,
    BT_HANDSFREE_SERVICE,
    BT_L2CAP_PROTOCOL_ID,
    BT_RFCOMM_PROTOCOL_ID,
)
from bumble.device import Device, DeviceConfiguration
from bumble.hci import Address, HCI_Write_Page_Timeout_Command
from bumble.sdp import (
    SDP_BLUETOOTH_PROFILE_DESCRIPTOR_LIST_ATTRIBUTE_ID,
    SDP_BROWSE_GROUP_LIST_ATTRIBUTE_ID,
    SDP_PROTOCOL_DESCRIPTOR_LIST_ATTRIBUTE_ID,
    SDP_PUBLIC_BROWSE_ROOT,
    SDP_SERVICE_CLASS_ID_LIST_ATTRIBUTE_ID,
    SDP_SERVICE_RECORD_HANDLE_ATTRIBUTE_ID,
    SDP_SUPPORTED_FEATURES_ATTRIBUTE_ID,
    DataElement,
    ServiceAttribute,
)
from bumble.snoop import BtSnooper
from bumble.transport import open_transport_or_link
from bumble.pairing import PairingConfig, PairingDelegate
from twister_harness import DeviceAdapter, Shell
# from bumble.pandora.security import SecurityService
# from bumble.pandora.config import Config
# from bumble.host import logger as host_logger

# class LogCaptureHandler(logging.Handler):
#     def __init__(self):
#         super().__init__()
#         self.log_records = []
 
#     def emit(self, record):
#         self.log_records.append(record)
 
#     def get_logs(self):
#         return [self.format(record) for record in self.log_records]
 
#     def clear_logs(self):
#         self.log_records = []
 
#     def check_contains(self, text):
#         return any(text in self.format(record) for record in self.log_records)

# def setup_logger_capture():
#     # host_logger.setLevel(logging.INFO)
#     capture_handler = LogCaptureHandler()
#     host_logger.addHandler(capture_handler)
#     return capture_handler

# host_handler = setup_logger_capture()
logger = logging.getLogger(__name__)

Mode="none"
L2CAP_SERVER_PSM=0x1001

def check_shell_response(lines: list[str], regex: str) -> bool:
    found = False
    try:
        for line in lines:
            if re.search(regex, line):
                found = True
                break
    except Exception as e:
        logger.error(f'{e}!', exc_info=True)
        raise e
    return found

async def wait_for_shell_response(dut, messages, max_wait_sec=10):
    found = False
    lines = []
    dct   = {}

    logger.debug(f'wait_for_shell_response') 

    for message in messages:
        dct[message] = False

    try:
        for i in range(0, max_wait_sec):
            read_lines = dut.readlines(print_output=False)
            for line in read_lines:
                logger.debug(f'DUT response: {str(line)}')
            lines = lines + read_lines
            for message in messages:
                for line in read_lines:
                    if re.search(message, line):
                        dct[message] = True
                        for key in dct:
                            if dct[key] is False:
                                found = False
                                break
                        else:
                            found = True
                        break
            if found is True:
                break 
            await asyncio.sleep(1)
    except Exception as e:
        logger.error(f'{e}!', exc_info=True)
        raise e

    for key in dct:
        logger.debug(f'Expected DUT response: "{key}", Matched: {dct[key]}') 

    return found, lines

async def send_cmd_to_iut(shell, dut, cmd, parse=None, wait=True):
    shell.exec_command(cmd)
    if parse is not None:
        lists = []
        lists.append(parse)
        found, lines = await wait_for_shell_response(dut, lists)
    else:
        found = True
        lines = []
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
    
async def smp_init_003(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< smp_init_003 ...')

    class Delegate(PairingDelegate):
        def __init__(
            self,
            io_capability,
        ):
            super().__init__(
                io_capability,
            )
            self.reset()

        def reset(self):
            self.number = asyncio.get_running_loop().create_future()

        async def display_number(self, number, digits):
            logger.info(f'displaying number: {number}')
            self.number.set_result(number)

    async with await open_transport_or_link(hci_port) as hci_transport:
        # device_config = DeviceConfiguration(
        #     name='Bumble',
        #     address=Address('F0:F1:F2:F3:F4:F5'),
        #     io_capability = PairingDelegate.IoCapability.DISPLAY_OUTPUT_AND_YES_NO_INPUT,
        # )

        # device = Device.from_config_with_hci(
        #     device_config,
        #     hci_transport.source,
        #     hci_transport.sink,
        # )

        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )

        device.classic_enabled = True
        device.le_enabled = False

        delegate = Delegate(PairingDelegate.IoCapability.DISPLAY_OUTPUT_AND_YES_NO_INPUT)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)

        logger.info(f'bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        # smp_config = Config()
        # SecurityService(device, smp_config)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            bumble_address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {bumble_address}")

            await send_cmd_to_iut(shell, dut, f"br connect {bumble_address}", "Connected:")

            iut_address = address.split(" ")[0]
            # connection = await bumble_acl_connect(shell, dut, device, iut_address)

            # await send_cmd_to_iut(shell, dut, f"bt auth all", None, wait=False)
            await send_cmd_to_iut(shell, dut, f"bt auth input", None, wait=False)
            # await send_cmd_to_iut(shell, dut, f"br l2cap connect 1001 {Mode}", " connected", wait=True)

            await send_cmd_to_iut(shell, dut, f"br l2cap connect  {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 3", None, wait=False) # "Confirm passkey"
            passkey = await delegate.number

            await send_cmd_to_iut(shell, dut, f"bt auth-passkey {passkey}", " connected", wait=True)

            # await send_cmd_to_iut(shell, dut, f"bt auth-passkey-confirm", " connected", wait=True)

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def smp_init_007(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< smp_init_007 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device_config = DeviceConfiguration(
            name='Bumble',
            address=Address('F0:F1:F2:F3:F4:F5'),
            io_capability = PairingDelegate.IoCapability.NO_OUTPUT_NO_INPUT,
            classic_sc_enabled = False,
            classic_ssp_enabled = False,
        )

        device = Device.from_config_with_hci(
            device_config,
            hci_transport.source,
            hci_transport.sink,
        )

        device.classic_enabled = True
        device.le_enabled = False

        logger.info(f'bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            bumble_address = device.public_address.to_string().split('/P')[0]
            logger.info(f"bumble address = {bumble_address}")

            await send_cmd_to_iut(shell, dut, f"br connect {bumble_address}", "Connected:")

            # await send_cmd_to_iut(shell, dut, f"bt security 1 1", None, wait=False)

            await send_cmd_to_iut(shell, dut, f"bt auth display", None, wait=False)

            await send_cmd_to_iut(shell, dut, f"br l2cap connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 1", " connected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def smp_init_011(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< smp_init_011 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device_config = DeviceConfiguration(
            name='Bumble',
            address=Address('F0:F1:F2:F3:F4:F5'),
            io_capability = PairingDelegate.IoCapability.NO_OUTPUT_NO_INPUT,
            classic_sc_enabled = True,
            classic_ssp_enabled = True,
        )

        device = Device.from_config_with_hci(
            device_config,
            hci_transport.source,
            hci_transport.sink,
        )

        device.classic_enabled = True
        device.le_enabled = False

        logger.info(f'bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            await send_cmd_to_iut(shell, dut, f"bt auth display", None, wait=False)

            bumble_address = device.public_address.to_string().split('/P')[0]

            # connection
            await send_cmd_to_iut(shell, dut, f"br connect {bumble_address}", f"Connected: {bumble_address}")
            shell.exec_command(f"br l2cap connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2")
            found, lines = await wait_for_shell_response(dut, [f"Bonded with {bumble_address}",
                                                               f"Security changed: {bumble_address} level 2",
                                                               f"Paired with {bumble_address}",
                                                               r"Channel \w+ connected"])
            assert found is True
            await send_cmd_to_iut(shell, dut, f"bt disconnect", f"Disconnected: {bumble_address}")

            # reconnection
            await send_cmd_to_iut(shell, dut, f"br connect {bumble_address}", f"Connected: {bumble_address}")
            shell.exec_command(f"br l2cap connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2")
            found, lines = await wait_for_shell_response(dut, [f"Security changed: {bumble_address} level 2",
                                                               r"Channel \w+ connected"])
            assert found is True
            found = check_shell_response(lines, f"Bonded with {bumble_address}")
            assert found is False
            found = check_shell_response(lines, f"Paired with {bumble_address}")
            assert found is False
            await send_cmd_to_iut(shell, dut, f"bt disconnect", f"Disconnected: {bumble_address}")

async def smp_init_014(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< smp_init_014 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device_config = DeviceConfiguration(
            name='Bumble',
            address=Address('F0:F1:F2:F3:F4:F5'),
            io_capability = PairingDelegate.IoCapability.NO_OUTPUT_NO_INPUT,
            classic_sc_enabled = True,
            classic_ssp_enabled = True,
        )

        device = Device.from_config_with_hci(
            device_config,
            hci_transport.source,
            hci_transport.sink,
        )

        device.classic_enabled = True
        device.le_enabled = False

        logger.info(f'bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            await send_cmd_to_iut(shell, dut, f"bt auth none", None, wait=False)

            bumble_address = device.public_address.to_string().split('/P')[0]

            await send_cmd_to_iut(shell, dut, f"br connect {bumble_address}", f"Connected: {bumble_address}")
            shell.exec_command(f"br l2cap connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2")
            found, lines = await wait_for_shell_response(dut, [f"Bonded with {bumble_address}",
                                                               f"Security changed: {bumble_address} level 2",
                                                               f"Paired with {bumble_address}",
                                                               r"Channel \w+ connected"])
            assert found is True
            await send_cmd_to_iut(shell, dut, f"bt disconnect", f"Disconnected: {bumble_address}")

async def smp_init_018(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< smp_init_018 ...')
    class Delegate(PairingDelegate):
        def __init__(
            self,
            io_capability,
        ):
            super().__init__(
                io_capability,
            )

        async def confirm(self, auto: bool = False) -> bool:
            await asyncio.sleep(60) # sleep for a long time to ensure DUT timeout
            return True

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )

        device.classic_enabled = True
        device.le_enabled = False

        delegate = Delegate(PairingDelegate.IoCapability.NO_OUTPUT_NO_INPUT)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)

        logger.info(f'bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            await send_cmd_to_iut(shell, dut, f"bt auth all", None, wait=False)

            bumble_address = device.public_address.to_string().split('/P')[0]

            await send_cmd_to_iut(shell, dut, f"br connect {bumble_address}", f"Connected: {bumble_address}")
            shell.exec_command(f"br l2cap connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2")
            found, lines = await wait_for_shell_response(dut, [f"Pairing failed with {bumble_address} reason: Authentication failure",
                                                               f"Security failed: {bumble_address}",
                                                               r"Channel \w+ disconnected"], max_wait_sec=60)
            assert found is True
            await send_cmd_to_iut(shell, dut, f"bt disconnect", None)

async def smp_rsp_002(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< smp_rsp_002 ...')

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

        async def get_number(self) -> Optional[int]:
            """
            Return an optional number as an answer to a passkey request.
            Returning `None` will result in a negative reply.
            """
            passkey = 0
            logger.debug(f'getting number')
            regex = r"Passkey for .*?: (?P<passkey>\d{6})"
            found, lines = await wait_for_shell_response(self.dut, [regex])
            if found is True:
                for line in lines:
                    searched = re.search(regex, line)
                    if searched:
                        passkey = int(searched.group("passkey"))
                        break

            logger.debug(f'Passkey = {passkey}')
            return passkey

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )

        device.classic_enabled = True
        device.le_enabled = False
        device.classic_sc_enabled = False

        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=False, mitm=True, bonding=True, delegate=delegate)

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            bumble_address = device.public_address.to_string().split('/P')[0]

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            iut_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, iut_address)

            await send_cmd_to_iut(shell, dut, f"bt auth display", None)
            await send_cmd_to_iut(shell, dut, f"br l2cap register {format(L2CAP_SERVER_PSM, 'x')} none sec 2", None)
            logger.debug(f'*** Authenticating...')
            await device.authenticate(connection)
            logger.debug(f'*** Authenticated')

            logger.debug(f'*** Enabling encryption...')
            await device.encrypt(connection)
            logger.debug(f'*** Encryption on')

            logger.debug(f'*** Creating L2CAP channel...')
            channel = await device.create_l2cap_channel(
                connection=connection,
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
            )
            logger.debug(f'*** L2CAP channel on...')

            found, lines = await wait_for_shell_response(dut, [f"Bonded with {bumble_address}",
                                                               f"Security changed: {bumble_address}",
                                                               r"Channel \w+ connected"])
            assert found is True
            await channel.disconnect()
            found, lines = await wait_for_shell_response(dut, [r"Channel \w+ disconnected"])
            assert found is True
            await device.disconnect(connection, HCI_REMOTE_USER_TERMINATED_CONNECTION_ERROR)
            found, lines = await wait_for_shell_response(dut, [f"Disconnected: {bumble_address}"])
            assert found is True

            # reconnection
            connection = await bumble_acl_connect(shell, dut, device, iut_address)
            logger.debug(f'*** Authenticating...')
            await device.authenticate(connection)
            logger.debug(f'*** Authenticated')

            logger.debug(f'*** Enabling encryption...')
            await device.encrypt(connection)
            logger.debug(f'*** Encryption on')

            logger.debug(f'*** Creating L2CAP channel...')
            channel = await device.create_l2cap_channel(
                connection=connection,
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
            )
            logger.debug(f'*** L2CAP channel on...')
            found, lines = await wait_for_shell_response(dut, [f"Security changed: {bumble_address}",
                                                               r"Channel \w+ connected"])
            assert found is True
            found = check_shell_response(lines, f"Bonded with {bumble_address}")
            assert found is False
            await channel.disconnect()
            found, lines = await wait_for_shell_response(dut, [r"Channel \w+ disconnected"])
            assert found is True
            await device.disconnect(connection, HCI_REMOTE_USER_TERMINATED_CONNECTION_ERROR)
            found, lines = await wait_for_shell_response(dut, [f"Disconnected: {bumble_address}"])
            assert found is True

class TestSmpServer:
    # def test_smp_init_003(self, shell: Shell, dut: DeviceAdapter, smp_initiator_dut):
    #     """Verify that an initiator can successfully establish a secure connection with
    #        a peripheral device when using "Keyboard Only" IO capability."""
    #     logger.info(f'smp_init_003 {smp_initiator_dut}')
    #     hci, iut_address = smp_initiator_dut
    #     asyncio.run(smp_init_003(hci, shell, dut, iut_address))

    # def test_smp_init_007(self, shell: Shell, dut: DeviceAdapter, smp_initiator_dut):
    #     """Verify that an initiator can successfully establish a secure connection by
    #        directly requesting a L2CAP connection without explicit pairing."""
    #     logger.info(f'smp_init_007 {smp_initiator_dut}')
    #     hci, iut_address = smp_initiator_dut
    #     asyncio.run(smp_init_007(hci, shell, dut, iut_address))

    # def test_smp_init_011(self, shell: Shell, dut: DeviceAdapter, smp_initiator_dut):
    #     """Verify that an initiator can successfully re-establish a secure connection with
    #        a previously paired device."""
    #     logger.info(f'test_smp_init_011 {smp_initiator_dut}')
    #     hci, iut_address = smp_initiator_dut
    #     asyncio.run(smp_init_011(hci, shell, dut, iut_address))

    # def test_smp_init_014(self, shell: Shell, dut: DeviceAdapter, smp_initiator_dut):
    #     """Verify that an initiator can successfully pair using the "Just Works" association model."""
    #     logger.info(f'test_smp_init_014 {smp_initiator_dut}')
    #     hci, iut_address = smp_initiator_dut
    #     asyncio.run(smp_init_014(hci, shell, dut, iut_address))

    # def test_smp_init_018(self, shell: Shell, dut: DeviceAdapter, smp_initiator_dut):
    #     """Verify that an initiator handles pairing timeout correctly."""
    #     logger.info(f'test_smp_init_018 {smp_initiator_dut}')
    #     hci, iut_address = smp_initiator_dut
    #     asyncio.run(smp_init_018(hci, shell, dut, iut_address))

    def test_smp_rsp_002(self, shell: Shell, dut: DeviceAdapter, smp_initiator_dut):
        """Verify SM responder functionality with Display Only capability and general bonding."""
        logger.info(f'test_smp_rsp_002 {smp_initiator_dut}')
        hci, iut_address = smp_initiator_dut
        asyncio.run(smp_rsp_002(hci, shell, dut, iut_address))
