# Copyright 2024 NXP
#
# SPDX-License-Identifier: Apache-2.0
import re
import asyncio
import logging
import sys
import time
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
L2CAP_CHAN_IUT_ID=0
Mode="base"
MAX_MTU="ffff" #65535
MIN_MTU="30" #48

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

async def bumble_authenticate(shell, dut, device, connection):
    try:
        await device.authenticate(connection)
        logger.info(f'=== authenticate successfully')
    except Exception as e:
        logger.error(f'Fail to authenticate!')
        raise e

async def bumble_acl_disconnect(shell, dut, device, connection, iut_id=L2CAP_CHAN_IUT_ID):
    await device.disconnect(connection, reason=HCI_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES_ERROR)
    found, lines = await wait_for_shell_response(dut, "Disconnected:")
    logger.info(f'lines : {lines}')
    assert found is True

    found = False
    for line in lines:
        if f"Channel {iut_id} disconnected" in line:
            logger.info(f'l2cap {iut_id} disconnect successfully')
            found = True
            break
    assert found is True

async def bumble_l2cap_disconnect(shell, dut, incoming_channel, iut_id=L2CAP_CHAN_IUT_ID):
    try:
        await incoming_channel.disconnect()
    except Exception as e:
        logger.error(f'Fail to send l2cap disconnect command!')
        raise e
    assert incoming_channel.disconnection_result is None

    found, lines = await wait_for_shell_response(dut, f"Channel {iut_id} disconnected")
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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            logger.info(f'=== Connecting to {target_address}...')
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect {L2CAP_CHAN_IUT_ID} successfully')
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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            check_data = f"Channel {L2CAP_CHAN_IUT_ID} disconnected"
            for line in lines:
                if check_data in line:
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

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

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
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

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
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

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
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
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
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

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

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

async def l2cap_case_14(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_14 ...')

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
    
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

async def l2cap_case_15(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_15 ...')

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
    
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_case_16(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_16 ...')

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

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            assert connection != None

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_case_17(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_17 ...')

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

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

async def l2cap_case_18(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_18')

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

async def l2cap_case_19(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_19 ...')

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_case_20(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_20 ...')

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

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            assert connection != None

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_case_21(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_21 ...')

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

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await bumble_authenticate(shell, dut, device, connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

async def l2cap_case_22(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_22 ...')

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
            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await bumble_authenticate(shell, dut, device, connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

async def l2cap_case_23(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_23 ...')

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

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break
            await bumble_authenticate(shell, dut, device, connection)
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_case_24(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_case_24 ...')

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

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await bumble_authenticate(shell, dut, device, connection)
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_case_25(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_25 ...')

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
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} mtu {MAX_MTU}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")


async def l2cap_case_26(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_26 ...')

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
        l2cap_server_1 = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM)
          )
        l2cap_server_2 = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=0x1003)
          )
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1003 {Mode}", "Channel 1 connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")
            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect 1", "Channel 1 disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
        
async def l2cap_case_27(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_27 ...')

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
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1002 {Mode}", "Unable to connect to psm", wait=False)

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_case_28(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_28 ...')

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
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_case_29(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_29 ...')

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
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode} mtu {MIN_MTU}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_case_30(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_case_30 ...')

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

        incoming_channel_1 = None
        incoming_channel_2 = None
        recv_data_1 = []
        recv_data_2 = []
        def get_channel_1(channel):
            nonlocal incoming_channel_1
            incoming_channel_1 = channel

            def on_recv_data_1(data):
                recv_data_1.append(data)
                logger.info(f"recv_data_1 = {data}")
            incoming_channel_1.sink = on_recv_data_1

        def get_channel_2(channel):
            nonlocal incoming_channel_2
            incoming_channel_2 = channel
            def on_recv_data_2(data):
                recv_data_2.append(data)
                logger.info(f"recv_data_1 = {data}")
            incoming_channel_2.sink = on_recv_data_2

        l2cap_server_1 = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM), handler=get_channel_1
          )
        l2cap_server_2 = device.create_l2cap_server(
                spec=ClassicChannelSpec(psm=0x1003), handler=get_channel_2
          )

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1001 {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client connect 1003 {Mode}", "Channel 1 connected")

            data = "this is test case 30, bumble send data to iut"
            data_ba = bytearray()
            data_ba.extend(data.encode('utf-8'))
            data_ba.append(0)
            logger.info(f"test l2cap send data {data}")   
            incoming_channel_1.send_pdu(data_ba)
            incoming_channel_2.send_pdu(data_ba)       
            found, lines = await wait_for_shell_response(dut,"Incoming data channel")
            count = 0
            for line in lines:
                if data in line:
                    count = count + 1
            assert count == 2

            data = "this_is_test_case_30_iut_send_data_to_bumble"
            logger.info(f"test l2cap recv data {data}")
            await send_cmd_to_iut(shell, dut, f"l2cap_client send {L2CAP_CHAN_IUT_ID} {data} {str(hex(len(data)))[2:]}", None, wait=False)
            await asyncio.sleep(0.5)
            logger.info(f"incoming_channel_1 recv data = {recv_data_1}")
            await send_cmd_to_iut(shell, dut, f"l2cap_client send 1 {data} {str(hex(len(data)))[2:]}", None, wait=False)
            await asyncio.sleep(0.5)
            logger.info(f"incoming_channel_2 recv data = {recv_data_2}")
            data_ba.clear()
            data_ba.extend(data.encode('utf-8'))
            recv_ba = bytearray()
            for data in recv_data_1:
                recv_ba.extend(data)
            assert data_ba == recv_ba
            recv_ba.clear()
            for data in recv_data_2:
                recv_ba.extend(data)
            assert data_ba == recv_ba

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"l2cap_client disconnect 1", f"Channel 1 disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")


async def l2cap_server_case_1(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_1 ...')

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
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device.power_on()
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_client register 1001 {Mode}", f"L2CAP psm {str(int(L2CAP_SERVER_PSM))} registered", wait=False)
           
            # l2cap_channle = await connection.create_l2cap_channel(ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            
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
        # def test_l2cap_case_13(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_13 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_13(hci, shell, dut, iut_address))
        # def test_l2cap_case_14(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_14 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_14(hci, shell, dut, iut_address))
        # def test_l2cap_case_15(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_15 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_15(hci, shell, dut, iut_address))
        # def test_l2cap_case_16(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_16 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_16(hci, shell, dut, iut_address))
        # def test_l2cap_case_17(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_17 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_17(hci, shell, dut, iut_address))
        # def test_l2cap_case_18(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_18 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_18(hci, shell, dut, iut_address))
        # def test_l2cap_case_19(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_19 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_19(hci, shell, dut, iut_address))
        # def test_l2cap_case_20(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_20 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_20(hci, shell, dut, iut_address))
        # def test_l2cap_case_21(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_21 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_21(hci, shell, dut, iut_address))
        # def test_l2cap_case_22(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_22 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_22(hci, shell, dut, iut_address))
        # def test_l2cap_case_23(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_23 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_23(hci, shell, dut, iut_address))
        # def test_l2cap_case_24(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_24 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_24(hci, shell, dut, iut_address))
        # def test_l2cap_case_25(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_25 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_25(hci, shell, dut, iut_address))
        # def test_l2cap_case_26(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_26 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_26(hci, shell, dut, iut_address))
        # def test_l2cap_case_27(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_27 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_27(hci, shell, dut, iut_address))
        # def test_l2cap_case_28(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_28 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_28(hci, shell, dut, iut_address))
        # def test_l2cap_case_29(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_29 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_29(hci, shell, dut, iut_address))
        # def test_l2cap_case_30(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
        #     logger.info(f'test_l2cap_case_30 {l2cap_client_dut}')
        #     hci, iut_address = l2cap_client_dut
        #     asyncio.run(l2cap_case_30(hci, shell, dut, iut_address))

        ######server
        def test_l2cap_server_case_1(self, shell: Shell, dut: DeviceAdapter, l2cap_client_dut): 
            logger.info(f'test_l2cap_server_case_1 {l2cap_client_dut}')
            hci, iut_address = l2cap_client_dut
            asyncio.run(l2cap_server_case_1(hci, shell, dut, iut_address))