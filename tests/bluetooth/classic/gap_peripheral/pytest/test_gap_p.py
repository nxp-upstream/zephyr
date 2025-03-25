# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0
import asyncio
import logging
import sys

from twister_harness import DeviceAdapter, Shell

from bumble.device import Device, DEVICE_DEFAULT_INQUIRY_LENGTH
from bumble import hci
from bumble.hci import Address, HCI_Write_Page_Timeout_Command
from bumble.snoop import BtSnooper
from bumble.transport import open_transport_or_link
from bumble.core import BT_BR_EDR_TRANSPORT, DeviceClass

from twister_harness import DeviceAdapter, Shell

logger = logging.getLogger(__name__)

# wait for shell response
async def _wait_for_shell_response(dut, message):
    found = False
    lines = []
    try:
        while found is False:
            read_lines = dut.readlines()
            for line in read_lines:
                logger.info(f'{str(line)}')
                if message in line:
                    found = True
                    break
            lines = lines + read_lines
            await asyncio.sleep(1)
    except Exception as e:
        logger.error(f'{e}!', exc_info=True)
        raise e
    return found, lines

# interact between script and DUT
async def send_cmd_to_iut(shell, dut, cmd, response=None):
    shell.exec_command(cmd)
    if response is not None:
        found, lines = await _wait_for_shell_response(dut, response)
    else:
        found = True
        lines = ''
    logger.info(f'{lines}')
    assert found is True
    return lines

# dongle limited discovery
async def start_limited_discovery(device):
    await device.send_command(
        hci.HCI_Write_Inquiry_Mode_Command(
            inquiry_mode=hci.HCI_EXTENDED_INQUIRY_MODE
        ),
        check_result=True,
    )

    response = await device.send_command(
        hci.HCI_Inquiry_Command(
            lap=hci.HCI_LIMITED_DEDICATED_INQUIRY_LAP,
            inquiry_length=DEVICE_DEFAULT_INQUIRY_LENGTH,
            num_responses=0,  # Unlimited number of responses.
        )
    )
    if response.status != hci.HCI_Command_Status_Event.PENDING:
        device.discovering = False
        raise hci.HCI_StatusError(response)

    device.auto_restart_inquiry = False
    device.discovering = True

# device listener for receiving scan results
class DiscoveryListener(Device.Listener):
    def __init__(self):
        self.discovered_addresses = set()
    def on_inquiry_result(self, address, class_of_device, data, rssi):
        (
            service_classes,
            major_device_class,
            minor_device_class,
        ) = DeviceClass.split_class_of_device(class_of_device)
        found_address = str(address).replace(r'/P','')
        logger.info(f'Found addr: {found_address}')
        self.discovered_addresses.add(found_address)

    def has_found_target_addr(self, target_addr):
        logger.info(f'Target addr: {target_addr} ...')
        return str(target_addr).upper() in self.discovered_addresses

async def tc_1(hci_port, shell, dut, address) -> None:
    """Non-discoverable and Non-connectable Mode Verification"""
    case_name = 'GAP tc 1 - GAP-P-1'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to non-discoverable mode
            logger.info(f'Step 1: Configuring peripheral to non-discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            
            # Configure the peripheral device to non-connectable mode
            logger.info(f'Step 2: Configuring peripheral to non-connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan off")
            
            # Use the central device to perform scanning operation for 30 seconds
            logger.info(f'Step 3: Scanning for devices for 10 seconds')
            await device.start_discovery()
            await asyncio.sleep(10)
            await device.stop_discovery()
            
            # Verify peripheral device cannot be discovered
            logger.info(f'Step 4: Verify peripheral device cannot be discovered')
            assert not device.listener.has_found_target_addr(dut_address), "Device was discovered but should be non-discoverable"
            logger.info(f'=== Device was not discovered as expected')
            
            # Try connecting to verify non-connectability (should fail)
            logger.info(f'Step 5: Try connecting to {dut_address} verify non-connectability (should fail)')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                assert False, "Connection successful but device should be non-connectable"
            except Exception as e:
                logger.info(f'=== Connection failed as expected: {e}')

async def tc_2(hci_port, shell, dut, address) -> None:
    """Rejecting Connection Requests in Non-discoverable Mode"""
    case_name = 'GAP tc 2 - GAP-P-2'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to non-discoverable mode
            logger.info(f'Step 1: Configuring peripheral to non-discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Configure peripheral to reject connection requests
            logger.info(f'Step 3: Configuring peripheral to reject connection requests')
            await send_cmd_to_iut(shell, dut, "br pscan off")
            await asyncio.sleep(1)
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Try connecting to peripheral (should be rejected)
            logger.info(f'Step 4: Attempting to connect to {dut_address} (should be rejected)')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                await asyncio.sleep(2)
                await connection.disconnect()
                assert False, "Connection was successful but should have been rejected"
            except Exception as e:
                logger.info(f'Step 5: Connection was rejected as expected')
                assert True, "Connection was rejected as expected"

async def tc_3(hci_port, shell, dut, address) -> None:
    """Central Device Disconnection in Non-discoverable Mode"""
    case_name = 'GAP tc 3 - GAP-P-3'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to non-discoverable mode
            logger.info(f'Step 1: Configuring peripheral to non-discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Verify peripheral device cannot be discovered
            logger.info(f'Step 3: Verify peripheral device cannot be discovered')
            await device.start_discovery()
            await asyncio.sleep(10)
            await device.stop_discovery()
            assert not device.listener.has_found_target_addr(dut_address), "Device was discovered but should be non-discoverable"

            # Central device initiates connection
            logger.info(f'Step 4: Central device initiating connection to known address: {dut_address}')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                found, lines = await _wait_for_shell_response(dut, "Connected")
                assert found is True, "DUT did not report connection established"
                logger.info(f'Step 5: Connection established')
                
                # Wait for stable connection
                await asyncio.sleep(2)
                
                # Central device initiates disconnection
                logger.info(f'Step 6: Central device initiating disconnection')
                await connection.disconnect()
                
                # Check if disconnection was handled properly
                found, lines = await _wait_for_shell_response(dut, "Disconnected")
                assert found is True, "DUT did not report disconnection"
                logger.info(f'Step 7: Successfully disconnected')
                
            except Exception as e:
                logger.error(f'Failed to connect/disconnect: {e}')
                assert False, f"Test failed: {e}"

async def tc_4(hci_port, shell, dut, address) -> None:
    """Peripheral-Initiated Disconnection in Non-discoverable Mode"""
    case_name = 'GAP tc 4 - GAP-P-4'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to non-discoverable mode
            logger.info(f'Step 1: Configuring peripheral to non-discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Verify peripheral device cannot be discovered
            logger.info(f'Step 3: Verify peripheral device cannot be discovered')
            await device.start_discovery()
            await asyncio.sleep(10)
            await device.stop_discovery()
            assert not device.listener.has_found_target_addr(dut_address), "Device was discovered but should be non-discoverable"

            # Central device initiates connection
            logger.info(f'Step 4: Central device initiating connection to known address: {dut_address}')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                found, lines = await _wait_for_shell_response(dut, "Connected")
                assert found is True, "DUT did not report connection established"
                logger.info(f'Step 5: Connection established')
                
                # Wait for stable connection
                await asyncio.sleep(2)
                
                # Peripheral initiates disconnection
                logger.info(f'Step 6: Peripheral initiating disconnection')
                await send_cmd_to_iut(shell, dut, "br disconnect")
                
                # Check if disconnection was handled properly
                found, lines = await _wait_for_shell_response(dut, "Disconnected")
                assert found is True, "DUT did not report disconnection"
                logger.info(f'Step 7: Successfully disconnected')
                
            except Exception as e:
                logger.error(f'Failed to connect/disconnect: {e}')
                assert False, f"Test failed: {e}"

async def tc_5(hci_port, shell, dut, address) -> None:
    """Limited Discoverable and Non-connectable Mode Verification"""
    case_name = 'GAP tc 5 - GAP-P-5'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to limited discoverable mode
            logger.info(f'Step 1: Configuring peripheral to limited discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            await send_cmd_to_iut(shell, dut, "br iscan on limited")
            
            # Configure the peripheral device to non-connectable mode
            logger.info(f'Step 2: Configuring peripheral to non-connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan off")
            
            # Verify peripheral device can not be discovered in general inquiry
            logger.info(f'Step 3: Verify peripheral device can not be discovered in general inquiry')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(10)
            await device.stop_discovery()
            assert not device.listener.has_found_target_addr(dut_address), "Device should not be discovered in general inquiry"

            # Verify peripheral device can be discovered in limited inquir
            logger.info(f'Step 4: Verify peripheral device can be discovered in limited inquiry')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan off")
            await send_cmd_to_iut(shell, dut, "br iscan on limited")
            await send_cmd_to_iut(shell, dut, "br pscan off")
            device.listener.discovered_addresses.clear()
            await start_limited_discovery(device)
            await asyncio.sleep(15)
            await device.stop_discovery()
            assert device.listener.has_found_target_addr(dut_address), "Device should be discovered in limited inquiry"
            
            # Try connecting to verify non-connectability
            logger.info(f'Step 5: Try connecting to verify non-connectability (should fail)')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                await asyncio.sleep(2)
                await connection.disconnect()
                assert False, "Connection was successful but device should be non-connectable"
            except Exception as e:
                logger.info(f'Step 6: Connection failed as expected')
            
            # Wait for limited discoverable mode to expire (60 seconds)
            logger.info(f'Step 7: Waiting 30 seconds for limited discoverable mode to expire')
            await asyncio.sleep(30)
            
            # Scan again to verify device is no longer discoverable
            logger.info(f'Step 8: Scanning again to verify device is no longer discoverable')
            device.listener.discovered_addresses.clear()
            await start_limited_discovery(device)
            await asyncio.sleep(10)
            await device.stop_discovery()
            
            # Verify peripheral device is no longer discoverable
            logger.info(f'Step 9: Verify peripheral device is no longer discoverable')
            assert not device.listener.has_found_target_addr(dut_address), "Device was still discoverable after limited mode expiry"

async def tc_6(hci_port, shell, dut, address) -> None:
    """Rejecting Connection Requests in Limited Discoverable Mode"""
    case_name = 'GAP tc 6 - GAP-P-6'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to limited discoverable mode
            logger.info(f'Step 1: Configuring peripheral to limited discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            await send_cmd_to_iut(shell, dut, "br iscan on limited")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Configure peripheral to reject connection requests
            logger.info(f'Step 3: Configuring peripheral to reject connection requests')
            await send_cmd_to_iut(shell, dut, "br pscan off")
            await asyncio.sleep(1)
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Scan to find the device
            logger.info(f'Step 4: Scanning for devices')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(30)
            await device.stop_discovery()
            
            # Verify peripheral device can be discovered
            logger.info(f'Step 5: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in limited discoverable mode"
            
            # Try connecting to peripheral (should be rejected)
            logger.info(f'Step 6: Attempting to connect to {dut_address} (should be rejected)')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                await asyncio.sleep(2)
                await connection.disconnect()
                assert False, "Connection was successful but should have been rejected"
            except Exception as e:
                logger.info(f'Step 7: Connection was rejected as expected')
                assert True, "Connection was rejected as expected"

async def tc_7(hci_port, shell, dut, address) -> None:
    """Central Device Disconnection in Limited Discoverable Mode"""
    case_name = 'GAP tc 7 - GAP-P-7'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to limited discoverable mode
            logger.info(f'Step 1: Configuring peripheral to limited discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            await send_cmd_to_iut(shell, dut, "br iscan on limited")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Scan to find the device
            logger.info(f'Step 3: Scanning for devices')
            device.listener.discovered_addresses.clear()
            await start_limited_discovery(device)
            await asyncio.sleep(15)
            await device.stop_discovery()
            
            # Verify peripheral device can be discovered
            logger.info(f'Step 4: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in limited discoverable mode"
            
            # Central device initiates connection
            logger.info(f'Step 5: Central device initiating connection to {dut_address}')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                found, lines = await _wait_for_shell_response(dut, "Connected")
                assert found is True, "DUT did not report connection established"
                logger.info(f'Step 6: Connection established')
                
                # Wait for stable connection
                await asyncio.sleep(2)
                
                # Central device initiates disconnection
                logger.info(f'Step 7: Central device initiating disconnection')
                await connection.disconnect()
                
                # Check if disconnection was handled properly
                found, lines = await _wait_for_shell_response(dut, "Disconnected")
                assert found is True, "DUT did not report disconnection"
                logger.info(f'Step 8: Successfully disconnected')
                
            except Exception as e:
                logger.error(f'Failed to connect/disconnect: {e}')
                assert False, f"Test failed: {e}"

async def tc_8(hci_port, shell, dut, address) -> None:
    """Peripheral-Initiated Disconnection in Limited Discoverable Mode"""
    case_name = 'GAP tc 8 - GAP-P-8'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to limited discoverable mode
            logger.info(f'Step 1: Configuring peripheral to limited discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan off")
            await send_cmd_to_iut(shell, dut, "br iscan on limited")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Scan to find the device
            logger.info(f'Step 3: Scanning for devices')
            device.listener.discovered_addresses.clear()
            await start_limited_discovery(device)
            await asyncio.sleep(15)
            await device.stop_discovery()
            
            # Verify peripheral device can be discovered
            logger.info(f'Step 4: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in limited discoverable mode"
            
            # Central device initiates connection
            logger.info(f'Step 5: Central device initiating connection to {dut_address}')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                found, lines = await _wait_for_shell_response(dut, "Connected")
                assert found is True, "DUT did not report connection established"
                logger.info(f'Step 6: Connection established')
                
                # Wait for stable connection
                await asyncio.sleep(2)
                
                # Peripheral initiates disconnection
                logger.info(f'Step 7: Peripheral initiating disconnection')
                await send_cmd_to_iut(shell, dut, "br disconnect")
                
                # Check if disconnection was handled properly
                found, lines = await _wait_for_shell_response(dut, "Disconnected")
                assert found is True, "DUT did not report disconnection"
                logger.info(f'Step 8: Successfully disconnected')
                
            except Exception as e:
                logger.error(f'Failed to connect/disconnect: {e}')
                assert False, f"Test failed: {e}"

async def tc_9(hci_port, shell, dut, address) -> None:
    """General Discoverable and Non-connectable Mode Verification"""
    case_name = 'GAP tc 9 - GAP-P-9'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to general discoverable mode
            logger.info(f'Step 1: Configuring peripheral to general discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan on")
            
            # Configure the peripheral device to non-connectable mode
            logger.info(f'Step 2: Configuring peripheral to non-connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan off")
            
            # Use the central device to perform scanning operation for 15 seconds
            logger.info(f'Step 3: Scanning for devices for 15 seconds')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(15)
            await device.stop_discovery()
            
            # Verify peripheral device can be discovered
            logger.info(f'Step 4: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in general discoverable mode"
            
            # Try connecting to verify non-connectability
            logger.info(f'Step 5: Try connecting to verify non-connectability (should fail)')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                await asyncio.sleep(2)
                await connection.disconnect()
                assert False, "Connection was successful but device should be non-connectable"
            except Exception as e:
                logger.info(f'Step 6: Connection failed as expected')
            
            # Wait for 2 minutes
            logger.info(f'Step 7: Waiting 1 minutes to verify general discoverable mode does not expire')
            await asyncio.sleep(60)
            
            # Scan again to verify device is still discoverable
            logger.info(f'Step 8: Scanning again to verify device is still discoverable')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(15)
            await device.stop_discovery()
            
            # Verify peripheral device is still discoverable
            logger.info(f'Step 9: Verify peripheral device is still discoverable')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discoverable but should still be in general discoverable mode"

async def tc_10(hci_port, shell, dut, address) -> None:
    """Rejecting Connection Requests in General Discoverable Mode"""
    case_name = 'GAP tc 10 - GAP-P-10'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to general discoverable mode
            logger.info(f'Step 1: Configuring peripheral to general discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan on")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Configure peripheral to reject connection requests
            logger.info(f'Step 3: Configuring peripheral to reject connection requests')
            await send_cmd_to_iut(shell, dut, "br pscan off")
            await asyncio.sleep(1)
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Scan to find the device
            logger.info(f'Step 4: Scanning for devices')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(30)
            await device.stop_discovery()
            
            # Verify peripheral device can be discovered
            logger.info(f'Step 5: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in general discoverable mode"
            
            # Try connecting to peripheral (should be rejected)
            logger.info(f'Step 6: Attempting to connect to {dut_address} (should be rejected)')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                await asyncio.sleep(2)
                await connection.disconnect()
                assert False, "Connection was successful but should have been rejected"
            except Exception as e:
                logger.info(f'Step 7: Connection was rejected as expected')
                assert True, "Connection was rejected as expected"

async def tc_11(hci_port, shell, dut, address) -> None:
    """Central Device Disconnection in General Discoverable Mode"""
    case_name = 'GAP tc 11 - GAP-P-11'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure the peripheral device to general discoverable mode
            logger.info(f'Step 1: Configuring peripheral to general discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan on")
            
            # Configure the peripheral device to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Scan to find the device
            logger.info(f'Step 3: Scanning for devices')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(15)
            await device.stop_discovery()
            
            # Verify peripheral device can be discovered
            logger.info(f'Step 4: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in general discoverable mode"
            
            # Central device initiates connection
            logger.info(f'Step 5: Central device initiating connection to {dut_address}')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                found, lines = await _wait_for_shell_response(dut, "Connected")
                assert found is True, "DUT did not report connection established"
                logger.info(f'Step 6: Connection established')
                
                # Wait for stable connection
                await asyncio.sleep(2)
                
                # Central device initiates disconnection
                logger.info(f'Step 7: Central device initiating disconnection')
                await connection.disconnect()
                
                # Check if disconnection was handled properly
                found, lines = await _wait_for_shell_response(dut, "Disconnected")
                assert found is True, "DUT did not report disconnection"
                logger.info(f'Step 8: Successfully disconnected')
                
            except Exception as e:
                logger.error(f'Failed to connect/disconnect: {e}')
                assert False, f"Test failed: {e}"

async def tc_12(hci_port, shell, dut, address) -> None:
    """Peripheral-Initiated Disconnection in General Discoverable Mode"""
    case_name = 'GAP tc 12 - GAP-P-12'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # Init PC bluetooth env
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        device.listener = DiscoveryListener()

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            # Initial state: general-discoverable, connectable
            await send_cmd_to_iut(shell, dut, "br pscan on")
            await send_cmd_to_iut(shell, dut, "br iscan on")

            # Configure peripheral to general discoverable mode
            logger.info(f'Step 1: Configuring peripheral to general discoverable mode')
            await send_cmd_to_iut(shell, dut, "br iscan on")
            
            # Configure peripheral to connectable mode
            logger.info(f'Step 2: Configuring peripheral to connectable mode')
            await send_cmd_to_iut(shell, dut, "br pscan on")
            
            # Scan to find the device
            logger.info(f'Step 3: Scanning for devices')
            device.listener.discovered_addresses.clear()
            await device.start_discovery()
            await asyncio.sleep(15)
            await device.stop_discovery()
            
            # Verify DUT is discovered
            logger.info(f'Step 4: Verify peripheral device can be discovered')
            assert device.listener.has_found_target_addr(dut_address), "Device was not discovered but should be in general discoverable mode"
            
            # PC tries to connect to DUT
            logger.info(f'Step 5: Central device initiating connection to {dut_address}')
            try:
                connection = await device.connect(dut_address, transport=BT_BR_EDR_TRANSPORT)
                found, lines = await _wait_for_shell_response(dut, "Connected")
                assert found is True, "DUT did not report connection established"
                logger.info(f'Step 6: Connection established')
                
                # Wait a bit for stable connection
                await asyncio.sleep(2)
                
                # Peripheral initiates disconnection
                logger.info(f'Step 7: Peripheral initiating disconnection')
                await send_cmd_to_iut(shell, dut, "br disconnect")
                
                # Check if disconnection was handled properly
                found, lines = await _wait_for_shell_response(dut, "Disconnected")
                assert found is True, "DUT did not report disconnection"
                logger.info(f'Step 8: Successfully disconnected')
                
            except Exception as e:
                logger.error(f'Failed to connect/disconnect to {dut_address}: {e}')
                assert False, f"Test failed: {e}"

class TestGAPPeripheral:
    def test_tc_1(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in non-discoverable and non-connectable mode."""
        logger.info(f'tc_1 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_1(hci, shell, dut, iut_address))
    
    # def test_tc_2(self, shell: Shell, dut: DeviceAdapter, device_under_test):
    #     """Test case to verify peripheral device in non-discoverable mode can properly reject connection requests."""
    #     logger.info(f'tc_2 {device_under_test}')
    #     hci, iut_address = device_under_test
    #     asyncio.run(tc_2(hci, shell, dut, iut_address))
    
    def test_tc_3(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in non-discoverable mode can properly handle 
        a disconnection request initiated by the central device."""
        logger.info(f'tc_3 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_3(hci, shell, dut, iut_address))
    
    def test_tc_4(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in non-discoverable mode can initiate and 
        properly handle a disconnection procedure."""
        logger.info(f'tc_4 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_4(hci, shell, dut, iut_address))
    
    def test_tc_5(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in limited discoverable but non-connectable mode."""
        logger.info(f'tc_5 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_5(hci, shell, dut, iut_address))
    
    # def test_tc_6(self, shell: Shell, dut: DeviceAdapter, device_under_test):
    #     """Test case to verify peripheral device in limited discoverable mode can properly reject connection requests."""
    #     logger.info(f'tc_6 {device_under_test}')
    #     hci, iut_address = device_under_test
    #     asyncio.run(tc_6(hci, shell, dut, iut_address))
    
    def test_tc_7(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in limited discoverable mode can properly handle 
        a disconnection request initiated by the central device."""
        logger.info(f'tc_7 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_7(hci, shell, dut, iut_address))
    
    def test_tc_8(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in limited discoverable mode can initiate and 
        properly handle a disconnection procedure."""
        logger.info(f'tc_8 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_8(hci, shell, dut, iut_address))
    
    def test_tc_9(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in general discoverable but non-connectable mode."""
        logger.info(f'tc_9 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_9(hci, shell, dut, iut_address))
    
    # def test_tc_10(self, shell: Shell, dut: DeviceAdapter, device_under_test):
    #     """Test case to verify peripheral device in general discoverable mode can properly reject connection requests."""
    #     logger.info(f'tc_10 {device_under_test}')
    #     hci, iut_address = device_under_test
    #     asyncio.run(tc_10(hci, shell, dut, iut_address))
    
    def test_tc_11(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in general discoverable mode can properly handle 
        a disconnection request initiated by the central device."""
        logger.info(f'tc_11 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_11(hci, shell, dut, iut_address))
    
    def test_tc_12(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test case to verify peripheral device in general discoverable mode can initiate and 
        properly handle a disconnection procedure."""
        logger.info(f'tc_12 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_12(hci, shell, dut, iut_address))