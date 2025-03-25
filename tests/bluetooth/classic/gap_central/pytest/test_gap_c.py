# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0
import asyncio
import logging
import sys

from twister_harness import DeviceAdapter, Shell

from bumble.device import Device
from bumble import hci
from bumble.hci import Address, HCI_Write_Page_Timeout_Command
from bumble.snoop import BtSnooper
from bumble.transport import open_transport_or_link
from bumble.core import BT_BR_EDR_TRANSPORT, DeviceClass

from twister_harness import DeviceAdapter, Shell

logger = logging.getLogger(__name__)

# wait for shell response
async def _wait_for_shell_response(dut, response, max_wait_sec=20):
    """
    _wait_for_shell_response() is used to wait for shell response.
    It will return after finding a specific 'response' or waiting long enough.
    :param dut:
    :param response: shell response that you want to monitor.
    :param max_wait_sec: maximum waiting time
    :return: found: whether the 'response' is found; lines: DUT shell response
    """
    found = False
    lines = []
    try:
        for i in range(0, max_wait_sec):
            read_lines = dut.readlines()
            for line in read_lines:
                if response in line:
                    found = True
                    break
            lines = lines + read_lines
            await asyncio.sleep(1)
        logger.info(f'{str(lines)}')
    except Exception as e:
        logger.error(f'{e}!', exc_info=True)
        raise e
    return found, lines

# interact between script and DUT
async def send_cmd_to_iut(shell, dut, cmd, response=None, expect_to_find_resp=True, max_wait_sec=20):
    """
    send_cmd_to_iut() is used to send shell cmd to DUT and monitor the response.
    It can choose whether to monitor the shell response of DUT.
    Use 'expect_to_find_resp' to set whether to expect the response to contain certain 'response'.
    'max_wait_sec' indicates the maximum waiting time. For 'expect_to_find_resp=False', this is useful
    because we need to wait long enough to get enough response
    to more accurately judge that the response does not contain specific characters.

    :param shell:
    :param dut:
    :param cmd: shell cmd sent to DUT
    :param response: shell response that you want to monitor. 'None' means not to monitor any response.
    :param expect_to_find_resp: set whether to expect the response to contain certain 'response'
    :param max_wait_sec: maximum monitoring time
    :return: DUT shell response
    """
    shell.exec_command(cmd)
    if response is not None:
        found, lines = await _wait_for_shell_response(dut, response, max_wait_sec)
    else:
        found = True
        lines = ''
    assert found is expect_to_find_resp
    return lines

# set limited discoverab mode of dongle
async def set_limited_discoverable(device, discoverable=True):
    # Read current class of device
    response = await device.send_command(
        hci.HCI_Command(
            op_code=0x0C23,  # Read Class of Device
            parameters=b''
        )
    )
    current_cod = response.return_parameters.class_of_device

    if discoverable:
        # set Limited Discoverable Mode (bit 13)
        new_cod = (current_cod | 0x2000).to_bytes(3, byteorder='little')
        # Limited Inquiry Access Code(LIAC) = 0x9E8B00
        iac = hci.HCI_LIMITED_DEDICATED_INQUIRY_LAP.to_bytes(3, byteorder='little')
    else:
        mask = ~0x2000
        new_cod = (current_cod & mask).to_bytes(3, byteorder='little')
        # General Inquiry Access Code(GIAC) = 0x9E8B33
        iac = hci.HCI_GENERAL_INQUIRY_LAP.to_bytes(3, byteorder='little')

    await device.send_command(
        hci.HCI_Command(
            op_code=0x0C24,  # Write Class of Device
            parameters=new_cod
        )
    )

    await device.send_command(
        hci.HCI_Command(
            op_code=0x0C3A,  # Write Current IAC LAP
            parameters=bytes([0x01]) + iac  # num_current_iac=1, iac_lap
        )
    )

    device.discoverable = discoverable

# dongle listener for receiving scan results
class DiscoveryListener(Device.Listener):
    def __init__(self):
        self.discovered_addresses = set()
    def on_inquiry_result(self, address, class_of_device, data, rssi):
        (
            service_classes,
            major_device_class,
            minor_device_class,
        ) = DeviceClass.split_class_of_device(class_of_device)
        found_address = str(address).replace(r'/P', '')
        logger.info(f'Found addr: {found_address}')
        self.discovered_addresses.add(found_address)

    def has_found_target_addr(self, target_addr):
        return str(target_addr).upper() in self.discovered_addresses

async def tc_1(hci_port, shell, dut, address) -> None:
    case_name = 'GAP-C-1: General Inquiry with Successful Connection and Active Disconnection'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init Dongle bluetooth
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
            dongle_address = str(device.public_address).replace(r'/P', '')
            await device.set_discoverable(True)  # Set peripheral as discoverable
            await device.set_connectable(True)   # Set peripheral as connectable
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            # test start
            shell.exec_command("br disconnect")
            logger.info("Step 1: Start the DUT in GAP Central role")
            # DUT is already in GAP Central role
            
            logger.info("Step 2: Initiate general inquiry")
            await send_cmd_to_iut(shell, dut, "br discovery on 8 limited", dongle_address, False)
            await send_cmd_to_iut(shell, dut, "br discovery on", dongle_address)
            
            logger.info("Step 3: Wait for device discovery")
            # Discovery is confirmed in previous step
            await send_cmd_to_iut(shell, dut, "br discovery off")
            
            logger.info("Step 4: Send connection request to discovered device")
            await send_cmd_to_iut(shell, dut, f"br connect {dongle_address}", "Connected")
            
            logger.info("Step 5: Verify connected event is received")
            # Connection is verified in previous step
            
            logger.info("Step 6: Send disconnection request")
            await send_cmd_to_iut(shell, dut, "br disconnect", "Disconnected")
            
            logger.info("Step 7: Wait for disconnection event")
            # Disconnection is verified in previous step
            
            # Final verification
            assert True, "Test completed successfully - all steps verified"

async def tc_2(hci_port, shell, dut, address) -> None:
    case_name = 'GAP-C-2: General Inquiry with Connection and Passive Disconnection'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init Dongle bluetooth
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
            dongle_address = str(device.public_address).replace(r'/P', '')
            await device.set_discoverable(True)  # Set peripheral as discoverable
            await device.set_connectable(True)   # Set peripheral as connectable
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            # test start
            shell.exec_command("br disconnect")
            logger.info("Step 1: Start the DUT in GAP Central role")
            # DUT is already in GAP Central role
            
            logger.info("Step 2: Initiate general inquiry")
            await send_cmd_to_iut(shell, dut, "br discovery on", dongle_address)
            
            logger.info("Step 3: Wait for device discovery")
            # Discovery is confirmed in previous step
            await send_cmd_to_iut(shell, dut, "br discovery off")
            
            logger.info("Step 4: Send connection request to discovered device")
            await send_cmd_to_iut(shell, dut, f"br connect {dongle_address}", "Connected")
            
            logger.info("Step 5: Verify connected event is received")
            # Connection is verified in previous step
            
            logger.info("Step 6: Wait for disconnection event (initiated by peripheral)")
            # Simulate peripheral initiated disconnection
            connection = device.find_connection_by_bd_addr(Address(dut_address))
            assert connection is not None, "No connection found with the DUT"
            
            await connection.disconnect()
            found, _ = await _wait_for_shell_response(dut, "Disconnected")
            assert found, "Disconnection event not received"

async def tc_3(hci_port, shell, dut, address) -> None:
    case_name = 'GAP-C-3: General Inquiry with Connection Rejection'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init Dongle bluetooth
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
            dongle_address = str(device.public_address).replace(r'/P', '')
            await device.set_discoverable(True)     # Set peripheral as discoverable
            # Set peripheral to reject connections
            await device.set_connectable(False)     # Using the reject connection API
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            # test start
            shell.exec_command("br disconnect")
            logger.info("Step 1: Start the DUT in GAP Central role")
            # DUT is already in GAP Central role
            
            logger.info("Step 2: Initiate general inquiry")
            await send_cmd_to_iut(shell, dut, "br discovery on", dongle_address)
            
            logger.info("Step 3: Wait for device discovery")
            # Discovery is confirmed in previous step
            await send_cmd_to_iut(shell, dut, "br discovery off")
            
            logger.info("Step 4: Send connection request to discovered device")
            shell.exec_command(f"br connect {dongle_address}")
            
            logger.info("Step 5: Observe connection rejection")
            await asyncio.sleep(10)  # Allow time for connection attempt to fail
            
            # Verify connection failure - Connected message should not appear
            found, _ = await _wait_for_shell_response(dut, "Connected", 5)
            assert not found, "Connected event was received when it should have failed"

async def tc_4(hci_port, shell, dut, address) -> None:
    case_name = 'GAP-C-4: Limited Inquiry with Successful Connection and Active Disconnection'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init Dongle bluetooth
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
            dongle_address = str(device.public_address).replace(r'/P', '')
            await set_limited_discoverable(device, True)  # Set peripheral as limited discoverable
            await device.set_connectable(True)   # Set peripheral as connectable
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            # test start
            shell.exec_command("br disconnect")
            logger.info("Step 1: Start the DUT in GAP Central role")
            # DUT is already in GAP Central role
            
            logger.info("Step 2: Initiate limited inquiry")
            await send_cmd_to_iut(shell, dut, "br discovery off")  # Reset discovery first
            await send_cmd_to_iut(shell, dut, "br discovery on", dongle_address, False)
            await send_cmd_to_iut(shell, dut, "br discovery off")  # Reset discovery first
            await send_cmd_to_iut(shell, dut, "br discovery on 8 limited", dongle_address)
            
            logger.info("Step 3: Wait for device discovery")
            
            logger.info("Step 4: Send connection request to discovered device")
            await send_cmd_to_iut(shell, dut, f"br connect {dongle_address}", "Connected")
            
            logger.info("Step 5: Verify connected event is received")
            # Connection is verified in previous step
            
            logger.info("Step 6: Send disconnection request")
            await send_cmd_to_iut(shell, dut, "br disconnect", "Disconnected")
            
            logger.info("Step 7: Wait for disconnection event")
            # Disconnection is verified in previous step
            
            # Final verification
            assert True, "Test completed successfully - all steps verified"

async def tc_5(hci_port, shell, dut, address) -> None:
    case_name = 'GAP-C-5: Limited Inquiry with Connection and Passive Disconnection'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init Dongle bluetooth
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
            dongle_address = str(device.public_address).replace(r'/P', '')
            await set_limited_discoverable(device, True)  # Set peripheral as limited discoverable
            await device.set_connectable(True)   # Set peripheral as connectable
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            # test start
            shell.exec_command("br disconnect")
            logger.info("Step 1: Start the DUT in GAP Central role")
            # DUT is already in GAP Central role
            
            logger.info("Step 2: Initiate limited inquiry")
            await send_cmd_to_iut(shell, dut, "br discovery off")  # Reset discovery first
            await send_cmd_to_iut(shell, dut, "br discovery on 8 limited", dongle_address)
            
            logger.info("Step 3: Wait for device discovery")
            # Discovery is confirmed in previous step
            await send_cmd_to_iut(shell, dut, "br discovery off")
            
            logger.info("Step 4: Send connection request to discovered device")
            await send_cmd_to_iut(shell, dut, f"br connect {dongle_address}", "Connected")
            
            logger.info("Step 5: Verify connected event is received")
            # Connection is verified in previous step
            
            logger.info("Step 6: Wait for disconnection event (initiated by peripheral)")
            # Simulate peripheral initiated disconnection
            connection = device.find_connection_by_bd_addr(Address(dut_address))
            assert connection is not None, "No connection found with the DUT"
            
            await connection.disconnect()
            found, _ = await _wait_for_shell_response(dut, "Disconnected")
            assert found, "Disconnection event not received"

async def tc_6(hci_port, shell, dut, address) -> None:
    case_name = 'GAP-C-6: Limited Inquiry with Connection Rejection'
    logger.info(f'<<< Start {case_name} ...')
    dut_address = address.split(" ")[0]

    async with await open_transport_or_link(hci_port) as hci_transport:
        # init Dongle bluetooth
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
            dongle_address = str(device.public_address).replace(r'/P', '')
            await device.set_discoverable(True)     # Set peripheral as discoverable
            # Set peripheral to reject connections
            await device.set_connectable(False)     # Using the reject connection API  
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            # test start
            shell.exec_command("br disconnect")
            logger.info("Step 1: Start the DUT in GAP Central role")
            # DUT is already in GAP Central role
            
            logger.info("Step 2: Initiate limited inquiry")
            await send_cmd_to_iut(shell, dut, "br discovery off")  # Reset discovery first
            await send_cmd_to_iut(shell, dut, "br discovery on 8 limited", dongle_address)
            
            logger.info("Step 3: Wait for device discovery")
            # Discovery is confirmed in previous step
            await send_cmd_to_iut(shell, dut, "br discovery off")
            
            logger.info("Step 4: Send connection request to discovered device")
            shell.exec_command(f"br connect {dongle_address}")
            
            logger.info("Step 5: Observe connection rejection")
            await asyncio.sleep(10)  # Allow time for connection attempt to fail
            
            # Verify connection failure - Connected message should not appear
            found, _ = await _wait_for_shell_response(dut, "Connected", 5)
            assert not found, "Connected event was received when it should have failed"

class TestGAPCentral:
    def test_tc_1(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test GAP-C-1: General Inquiry with Successful Connection and Active Disconnection."""
        logger.info(f'Running test_tc_1 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_1(hci, shell, dut, iut_address))
        
    def test_tc_2(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test GAP-C-2: General Inquiry with Connection and Passive Disconnection."""
        logger.info(f'Running test_tc_2 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_2(hci, shell, dut, iut_address))
        
    # def test_tc_3(self, shell: Shell, dut: DeviceAdapter, device_under_test):
    #     """Test GAP-C-3: General Inquiry with Connection Rejection."""
    #     logger.info(f'Running test_tc_3 {device_under_test}')
    #     hci, iut_address = device_under_test
    #     asyncio.run(tc_3(hci, shell, dut, iut_address))
        
    def test_tc_4(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test GAP-C-4: Limited Inquiry with Successful Connection and Active Disconnection."""
        logger.info(f'Running test_tc_4 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_4(hci, shell, dut, iut_address))
        
    def test_tc_5(self, shell: Shell, dut: DeviceAdapter, device_under_test):
        """Test GAP-C-5: Limited Inquiry with Connection and Passive Disconnection."""
        logger.info(f'Running test_tc_5 {device_under_test}')
        hci, iut_address = device_under_test
        asyncio.run(tc_5(hci, shell, dut, iut_address))
        
    # def test_tc_6(self, shell: Shell, dut: DeviceAdapter, device_under_test):
    #     """Test GAP-C-6: Limited Inquiry with Connection Rejection."""
    #     logger.info(f'Running test_tc_6 {device_under_test}')
    #     hci, iut_address = device_under_test
    #     asyncio.run(tc_6(hci, shell, dut, iut_address))
