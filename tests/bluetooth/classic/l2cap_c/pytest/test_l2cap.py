# Copyright 2024 NXP
#
# SPDX-License-Identifier: Apache-2.0
import re
import asyncio
import logging
import sys
from enum import Enum
from bumble.core import *
from bumble.l2cap import *
from bumble.hci import *
from bumble.l2cap import (
    L2CAP_Connection_Request,
    ClassicChannelSpec,
    LeCreditBasedChannelSpec,
)
from bumble.device import Device
from bumble.hci import Address, HCI_Write_Page_Timeout_Command
from bumble.snoop import BtSnooper
from bumble.transport import open_transport_or_link
from twister_harness import DeviceAdapter, Shell
logger = logging.getLogger(__name__)

L2CAP_SERVER_PSM=0x1001
Mode="base"

async def wait_for_shell_response(dut, message):
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

async def send_cmd_to_iut(shell, dut, cmd, parse=None):
    shell.exec_command(cmd)
    if parse is not None:
        found, lines = await wait_for_shell_response(dut, parse)
    else:
        found = True
        lines = ''
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

async def bumble_authenticate(shell, dut, device, connection):
    try:
        await device.authenticate(connection)
        logger.info(f'=== authenticate successfully')
    except Exception as e:
        logger.error(f'Fail to authenticate!')
        raise e

async def bumble_acl_disconnect(shell, dut, device, connection):
    await device.disconnect(connection, reason=HCI_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES_ERROR)
    found, lines = await wait_for_shell_response(dut, "Disconnected:")
    logger.info(f'lines : {lines}')
    assert found is True

    found = False
    for line in lines:
        if "Channel disconnected" in line:
            logger.info(f'l2cap disconnect successfully')
            found = True
            break
    assert found is True

async def bumble_l2cap_disconnect(shell, dut, incoming_channel):
    try:
        await incoming_channel.disconnect()
    except Exception as e:
        logger.error(f'Fail to send l2cap disconnect command!')
        raise e
    assert incoming_channel.disconnection_result is None

    found, lines = await wait_for_shell_response(dut, "Channel disconnected")
    assert found is True


async def l2cap_case_1(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_1 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
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

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect", "Channel disconnected")

async def l2cap_case_2(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_2 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info(f'bumble l2cap server register.')
        incoming_channel = None
        def get_channel(channel):
                nonlocal incoming_channel
                incoming_channel = channel

        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel
          ) 
    
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]
            logger.info(f'=== Connecting to {target_address}...')

            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)
            


async def l2cap_case_3(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_3 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info('bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
                        
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if "Channel disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_case_4(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_4 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info('bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_case_5(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_5 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        
        logger.info('bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", "Channel connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect", "Channel disconnected")

async def l2cap_case_6(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_6')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info('bumble l2cap server register.')
        incoming_channel = None
        def get_channel(channel):
                nonlocal incoming_channel
                incoming_channel = channel

        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel
          ) 
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", "Channel connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

async def l2cap_case_7(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_7 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        
        logger.info('bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]

            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", "Channel connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if "Channel disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_case_8(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_8 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        
        logger.info('bumble l2cap server register.')
        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]

            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", "Channel connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_case_9(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_9 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
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
            
            target_address = address.split(" ")[0]

            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await bumble_authenticate(shell, dut, device, connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect", "Channel disconnected")

async def l2cap_case_10(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_10 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info(f'bumble l2cap server register.')
        incoming_channel = None
        def get_channel(channel):
                nonlocal incoming_channel
                incoming_channel = channel

        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel
          ) 
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            logger.info(f'=== Connecting to {target_address}...')
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            await bumble_authenticate(shell, dut, device, connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

async def l2cap_case_11(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_11 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info(f'bumble l2cap server register.')
        incoming_channel = None
        def get_channel(channel):
                nonlocal incoming_channel
                incoming_channel = channel

        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel
          ) 
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            await bumble_authenticate(shell, dut, device, connection)
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if "Channel disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_case_12(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_12 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info(f'bumble l2cap server register.')
        incoming_channel = None
        def get_channel(channel):
                nonlocal incoming_channel
                incoming_channel = channel

        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel
          ) 
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            await bumble_authenticate(shell, dut, device, connection)
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_case_13(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_13 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
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

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect", "Channel disconnected")

async def l2cap_active_acl_conn_active_l2cap_disconn(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap client active acl connect  active l2cap disconnect ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        logger.info(f'bumble l2cap server register.')
        incoming_channel = None
        def get_channel(channel):
                nonlocal incoming_channel
                incoming_channel = channel

        l2cap_server = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel
          ) 
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", "Channel connected")
        
            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect", "Channel disconnected")


class TestL2capServer:
        # def test_l2cap_case_1(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut):  
        #     logger.info(f'test_l2cap_case_1 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_1(hci, shell, dut, iut_address))
        # def test_l2cap_case_2(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_2 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_2(hci, shell, dut, iut_address))
        # def test_l2cap_case_3(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut):  
        #     logger.info(f'test_l2cap_case_3 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_3(hci, shell, dut, iut_address))
        # def test_l2cap_case_4(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut):  
        #     logger.info(f'test_l2cap_case_4 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_4(hci, shell, dut, iut_address))
        # def test_l2cap_case_5(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut):  
        #     logger.info(f'test_l2cap_case_5 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_5(hci, shell, dut, iut_address))
        # def test_l2cap_case_6(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_6 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_6(hci, shell, dut, iut_address))
        # def test_l2cap_case_7(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut):  
        #     logger.info(f'test_l2cap_case_7 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_7(hci, shell, dut, iut_address))
        # def test_l2cap_case_8(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_8 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_8(hci, shell, dut, iut_address))
        # def test_l2cap_case_9(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_9 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_9(hci, shell, dut, iut_address))
        # def test_l2cap_case_10(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_10 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_10(hci, shell, dut, iut_address))
        # def test_l2cap_case_11(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_11 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_11(hci, shell, dut, iut_address))
        # def test_l2cap_case_12(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_12 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_12(hci, shell, dut, iut_address))
        def test_l2cap_case_13(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
            logger.info(f'test_l2cap_case_13 {l2cap_client_dut}')
            hci, iut_address = l2cap_client_dut
            asyncio.run(l2cap_case_13(hci, shell, dut, iut_address))

        # def test_l2cap_active_acl_conn_active_l2cap_disconnect(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut):  #acl conn -> l2cap con ->l2cap discon
        #     logger.info(f'test_l2cap_active_acl_conn_active_l2cap_disconnect {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_active_acl_conn_active_l2cap_disconn(hci, shell, dut, iut_address))




