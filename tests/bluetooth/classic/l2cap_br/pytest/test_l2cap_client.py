# Copyright 2025 NXP

# SPDX-License-Identifier: Apache-2.0
from test_l2cap_common import *

async def l2cap_client_case_1(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_1 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_2(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_2 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)
            
            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_3(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_3 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect {L2CAP_CHAN_IUT_ID} successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_4(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_4 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            found, lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    found = True
                    break
            assert found is True

async def l2cap_client_case_5(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_5 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")
            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_6(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_6')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)
            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_7(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_7 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            check_data = f"Channel {L2CAP_CHAN_IUT_ID} disconnected"
            for line in lines:
                if check_data in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_8(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_8 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_9(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_9 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await device.authenticate(connection)
            await device.encrypt(connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_10(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_10 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)  

        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await device.authenticate(connection)
            await device.encrypt(connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_11(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_11 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await device.authenticate(connection)
            await device.encrypt(connection)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_12(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_12 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
            
            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)

            await device.authenticate(connection)
            await device.encrypt(connection)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_client_case_13(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_13 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")


async def l2cap_client_case_14(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_14 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")
    
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

async def l2cap_client_case_15(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_15 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")
    
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_16(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_16 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break
            assert connection != None

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            found, lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    found = True
                    break
            assert found is True

async def l2cap_client_case_17(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_17 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_client_case_18(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_18')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)
            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_client_case_19(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_19 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_20(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_20 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break
            assert connection != None

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} sec 2", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            found,lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_21(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_21 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))
            
            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            logger.info(f"address = {address}")

            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")

            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await device.authenticate(connection)
            await device.encrypt(connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_client_case_22(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_22 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")
            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await device.authenticate(connection)
            await device.encrypt(connection)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_client_case_23(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_23 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)
        

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")
            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await device.authenticate(connection)
            await device.encrypt(connection)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_24(hci_port, shell, dut, address) -> None:
    logger.info('<<< l2cap_client_case_24 ...')

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
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            address = device.public_address.to_string().split('/P')[0]
            await send_cmd_to_iut(shell, dut, f"br connect {address}", "Connected:")
            for conn in device.connections.values():
                if conn.self_address.to_string().split('/P')[0] == address.split(" ")[0]:
                    connection = conn
                    break

            await device.authenticate(connection)
            await device.encrypt(connection)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            found,lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect successfully')
                    found = True
                    break
            assert found is True

async def l2cap_client_case_25(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_25 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} mtu {format(MAX_MTU, 'x')}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")


async def l2cap_client_case_26(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_26 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect 1003 {Mode}", "Channel 1 connected")

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
            await send_cmd_to_iut(shell, dut, f"l2cap_br send {L2CAP_CHAN_IUT_ID} {data} {str(hex(len(data)))[2:]}", None, wait=False)
            await asyncio.sleep(0.5)
            logger.info(f"incoming_channel_1 recv data = {recv_data_1}")
            await send_cmd_to_iut(shell, dut, f"l2cap_br send 1 {data} {str(hex(len(data)))[2:]}", None, wait=False)
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

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect 1", f"Channel 1 disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
        
async def l2cap_client_case_27(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_27 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect 1002 {Mode}", "Unable to connect to psm", wait=False)

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_client_case_28(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_28 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await bumble_l2cap_disconnect(shell, dut, incoming_channel)

            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"Channel {L2CAP_CHAN_IUT_ID} connected")
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")

async def l2cap_client_case_29(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_client_case_29 ...')

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
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            
            await send_cmd_to_iut(shell, dut, f"l2cap_br connect {format(L2CAP_SERVER_PSM, 'x')} {Mode} mtu {format(MIN_MTU, 'x')}", f"Channel {L2CAP_CHAN_IUT_ID} connected")

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")


class TestL2capBRClient:
        def test_l2cap_client_case_1(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):  
            """Test passive ACL connection,l2cap connect and active l2cap disconnect"""
            logger.info(f'test_l2cap_client_case_1 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_1(hci, shell, dut, iut_address))
        def test_l2cap_client_case_2(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):
            """Test passive ACL connection,l2cap connect, and passive l2cap disconnect""" 
            logger.info(f'test_l2cap_client_case_2 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_2(hci, shell, dut, iut_address))
        def test_l2cap_client_case_3(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):  
            """Test passive ACL connection,l2cap connect and active acl disconnect. l2cap disconnect should be successfully.""" 
            logger.info(f'test_l2cap_client_case_3 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_3(hci, shell, dut, iut_address))
        def test_l2cap_client_case_4(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):  
            """Test passive ACL connection,l2cap connect and passive acl disconnect. l2cap disconnect should be successfully.""" 
            logger.info(f'test_l2cap_client_case_4 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_4(hci, shell, dut, iut_address))
        def test_l2cap_client_case_5(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):  
            """Test passive ACL connection,l2cap connect,active authenticate and active l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_5 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_5(hci, shell, dut, iut_address))
        def test_l2cap_client_case_6(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect,active authenticate and passive l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_6 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_6(hci, shell, dut, iut_address))
        def test_l2cap_client_case_7(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):  
            """Test passive ACL connection,l2cap connect,active authenticate and active acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_7 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_7(hci, shell, dut, iut_address))
        def test_l2cap_client_case_8(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect,active authenticate and passive acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_8 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_8(hci, shell, dut, iut_address))
        def test_l2cap_client_case_9(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect,passive authenticate and active l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_9 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_9(hci, shell, dut, iut_address))
        def test_l2cap_client_case_10(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect,passive authenticate and passive l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_10 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_10(hci, shell, dut, iut_address))
        def test_l2cap_client_case_11(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection, l2cap connect,passive authenticate and active acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_11 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_11(hci, shell, dut, iut_address))
        def test_l2cap_client_case_12(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection, l2cap connect,passive authenticate and passive acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_12 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_12(hci, shell, dut, iut_address))
        def test_l2cap_client_case_13(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect and active l2cap disconnect"""
            logger.info(f'test_l2cap_client_case_13 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_13(hci, shell, dut, iut_address))
        def test_l2cap_client_case_14(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect, and passive l2cap disconnect""" 
            logger.info(f'test_l2cap_client_case_14 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_14(hci, shell, dut, iut_address))
        def test_l2cap_client_case_15(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect and active acl disconnect. l2cap disconnect should be successfully.""" 
            logger.info(f'test_l2cap_client_case_15 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_15(hci, shell, dut, iut_address))
        def test_l2cap_client_case_16(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect and passive acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_16 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_16(hci, shell, dut, iut_address))
        def test_l2cap_client_case_17(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect,active authenticate and active l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_17 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_17(hci, shell, dut, iut_address))
        def test_l2cap_client_case_18(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect,active authenticate and passive l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_18 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_18(hci, shell, dut, iut_address))
        def test_l2cap_client_case_19(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect,active authenticate and active acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_19 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_19(hci, shell, dut, iut_address))
        def test_l2cap_client_case_20(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect,active authenticate and passive acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_20 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_20(hci, shell, dut, iut_address))
        def test_l2cap_client_case_21(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect,passive authenticate and active l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_21 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_21(hci, shell, dut, iut_address))
        def test_l2cap_client_case_22(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect,passive authenticate and passive l2cap disconnect."""
            logger.info(f'test_l2cap_client_case_22 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_22(hci, shell, dut, iut_address))
        def test_l2cap_client_case_23(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection, l2cap connect,passive authenticate and active acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_23 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_23(hci, shell, dut, iut_address))
        def test_l2cap_client_case_24(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection, l2cap connect,passive authenticate and passive acl disconnect. l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_client_case_24 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_24(hci, shell, dut, iut_address))
        def test_l2cap_client_case_25(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """ Test l2cap connection with max MTU(0xffff).But the max mtu which the stack supports is (CONFIG_BT_BUF_ACL_RX_SIZE - 4U = 196)."""
            logger.info(f'test_l2cap_client_case_25 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_25(hci, shell, dut, iut_address))
        def test_l2cap_client_case_26(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test l2cap multi_channel connection and data tranfer. Two PSM(0x1001, 0x1003) are registered in Bumble which is server""" 
            logger.info(f'test_l2cap_client_case_26 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_26(hci, shell, dut, iut_address))
        def test_l2cap_client_case_27(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """ Test l2cap connection with invaild PSM."""
            logger.info(f'test_l2cap_client_case_27 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_27(hci, shell, dut, iut_address))
        def test_l2cap_client_case_28(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut):
            """ Test l2cap re_connect after disconnected.""" 
            logger.info(f'test_l2cap_client_case_28 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_28(hci, shell, dut, iut_address))
        def test_l2cap_client_case_29(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test l2cap connection with min MTU(0x30)."""
            logger.info(f'test_l2cap_client_case_29 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_client_case_29(hci, shell, dut, iut_address))
