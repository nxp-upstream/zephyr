# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

from test_l2cap_common import *

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

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_server_case_2(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_2 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            await l2cap_channel.disconnect()
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} disconnected")
            assert found is True
            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_server_case_3(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_3 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect {L2CAP_CHAN_IUT_ID} successfully')
                    found = True
                    break
            assert found is True

async def l2cap_server_case_4(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_4 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            found, lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    found = True
                    break
            assert found is True

async def l2cap_server_case_5(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_5 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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
            assert connection != None

            await device.authenticate(connection)
            await device.encrypt(connection)

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_server_case_6(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_6 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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
            assert connection != None

            await device.authenticate(connection)
            await device.encrypt(connection)

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            await l2cap_channel.disconnect()
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} disconnected")
            assert found is True

            await bumble_acl_disconnect(shell, dut, device, connection)

async def l2cap_server_case_7(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_7 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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
            assert connection != None

            await device.authenticate(connection)
            await device.encrypt(connection)
            
            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            lines = await send_cmd_to_iut(shell, dut, f"bt disconnect", "Disconnected:")
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    logger.info(f'l2cap disconnect {L2CAP_CHAN_IUT_ID} successfully')
                    found = True
                    break
            assert found is True

async def l2cap_server_case_8(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_8 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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

            assert connection != None

            # await send_cmd_to_iut(shell, dut, f"l2cap_br register {format(L2CAP_SERVER_PSM, 'x')} {Mode}", f"L2CAP psm {str(int(L2CAP_SERVER_PSM))} registered", wait=False)
            logger.info(f'authenticate START **************************')
            await device.authenticate(connection)
            logger.info(f'authenticate END **************************')

            logger.info(f'encrypt START **************************')
            await device.encrypt(connection)
            logger.info(f'encrypt END **************************')
            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            found, lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    found = True
                    break
            assert found is True

async def l2cap_server_case_9(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_9 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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
            assert connection != None

            await device.authenticate(connection)
            await device.encrypt(connection)

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM,mtu=65535))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            found, lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    found = True
                    break
            assert found is True

async def l2cap_server_case_10(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_10 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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
            assert connection != None

            await device.authenticate(connection)
            await device.encrypt(connection)

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM,mtu=48))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            found, lines = await bumble_acl_disconnect(shell, dut, device, connection)
            found = False
            for line in lines:
                if f"Channel {L2CAP_CHAN_IUT_ID} disconnected" in line:
                    found = True
                    break
            assert found is True

async def l2cap_server_case_11(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_11 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
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

            assert connection != None

            await device.authenticate(connection)
            await device.encrypt(connection)

            try:
                await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=0x1003))
            except ProtocolError as error:
                logger.info(f'error_code = {error.error_code}')
                logger.info(f'error_name = {error.error_name}')
                assert error.error_code == 0x2 and error.error_name == 'CONNECTION_REFUSED_PSM_NOT_SUPPORTED'

async def l2cap_server_case_12(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_12 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        client0_received = []
        client1_received = []
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

            assert connection != None

            await send_cmd_to_iut(shell, dut, f"l2cap_br register 1003 {Mode}", f"L2CAP psm {str(int(0x1003))} registered", wait=False)

            await device.authenticate(connection)
            await device.encrypt(connection)

            l2cap_channel0 = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            def on_client0_data(data):
                client0_received.append(data)
            l2cap_channel0.sink =  on_client0_data 
            l2cap_channel1 = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=0x1003))
            found, lines = await wait_for_shell_response(dut, f"Channel 1 connected")
            assert found is True   
            def on_client1_data(data):
                client1_received.append(data)
            l2cap_channel1.sink =  on_client1_data 

            data = "this is server case 13,bumble send data to iut"
            data_ba = bytearray()
            data_ba.extend(data.encode('utf-8'))
            data_ba.append(0)
            logger.info(f"test l2cap server recv data {data}")   
            l2cap_channel0.send_pdu(data_ba)
            l2cap_channel1.send_pdu(data_ba)       
            found, lines = await wait_for_shell_response(dut,"Incoming data channel")
            count = 0
            for line in lines:
                if data in line:
                    count = count + 1
            assert count == 2

            data = "this_is_server_case_13_iut_send_data_to_bumble"
            logger.info(f"test l2cap send data {data}")
            await send_cmd_to_iut(shell, dut, f"l2cap_br send {L2CAP_CHAN_IUT_ID} {data} {str(hex(len(data)))[2:]}", None, wait=False)
            await asyncio.sleep(0.5)
            logger.info(f"l2cap_channel0 recv data = {client0_received}")
            await send_cmd_to_iut(shell, dut, f"l2cap_br send 1 {data} {str(hex(len(data)))[2:]}", None, wait=False)
            await asyncio.sleep(0.5)
            logger.info(f"l2cap_channel1 recv data = {client1_received}")
            data_ba.clear()
            data_ba.extend(data.encode('utf-8'))
            recv_ba = bytearray()
            for data in client0_received:
                recv_ba.extend(data)
            assert data_ba == recv_ba
            recv_ba.clear()
            for data in client1_received:
                recv_ba.extend(data)
            assert data_ba == recv_ba

            found, lines = await bumble_acl_disconnect(shell, dut, device, connection) 
            found0 = False
            found1 = False    
            for line in lines:
                if line.find("Channel 0 disconnected") != -1:
                    found0 = True
                elif line.find("Channel 1 disconnected") != -1:
                    found1 = True
            assert found0 is True and found1 is True


async def l2cap_server_case_13(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_13 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            data_ba = bytearray()
            data_recv = "this is server case 13,bumble send data to iut"
            data_send = "this_is_server_case_13_iut_send_data_to_bumble"
            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            client_received = []
            def on_client_data(data):
                client_received.append(data)

            logger.info(f'authenticate START **************************')
            await device.authenticate(connection)
            logger.info(f'authenticate END **************************')

            logger.info(f'encrypt START **************************')
            await device.encrypt(connection)
            logger.info(f'encrypt END **************************')

            for count in range(STRESS_TEST_MAX_COUNT):
                l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
                found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
                assert found is True
                await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await bumble_acl_disconnect(shell, dut, device, connection)


async def l2cap_server_case_14(hci_port, shell, dut, address) -> None:
    logger.info(f'<<< l2cap_server_case_14 ...')

    async with await open_transport_or_link(hci_port) as hci_transport:
        device = Device.with_hci(
            'Bumble',
            Address('F0:F1:F2:F3:F4:F5'),
            hci_transport.source,
            hci_transport.sink,
        )
        device.classic_enabled = True
        device.le_enabled = False
        delegate = Delegate(dut, PairingDelegate.IoCapability.KEYBOARD_INPUT_ONLY)
        device.pairing_config_factory = lambda connection: \
                                        PairingConfig(sc=True, mitm=True, bonding=True, delegate=delegate)
        
        with open(f"bumble_hci_{sys._getframe().f_code.co_name}.log", "wb") as snoop_file:
            data_ba = bytearray()
            data_recv = "this is server case 13,bumble send data to iut"
            data_send = "this_is_server_case_13_iut_send_data_to_bumble"

            device.host.snooper = BtSnooper(snoop_file)
            await device_power_on(device)
            await device.send_command(HCI_Write_Page_Timeout_Command(page_timeout=0xFFFF))

            await send_cmd_to_iut(shell, dut, f"bt clear all", None)

            target_address = address.split(" ")[0]
            connection = await bumble_acl_connect(shell, dut, device, target_address)
            client_received = []
            def on_client_data(data):
                client_received.append(data)

            logger.info(f'authenticate START **************************')
            await device.authenticate(connection)
            logger.info(f'authenticate END **************************')

            logger.info(f'encrypt START **************************')
            await device.encrypt(connection)
            logger.info(f'encrypt END **************************')

            l2cap_channel = await connection.create_l2cap_channel(spec=ClassicChannelSpec(psm=L2CAP_SERVER_PSM))
            found, lines = await wait_for_shell_response(dut, f"Channel {L2CAP_CHAN_IUT_ID} connected")
            assert found is True
            l2cap_channel.sink = on_client_data 
            for count in range(STRESS_TEST_MAX_COUNT):
                logger.info(f"test l2cap server recv data {data_recv}")
                data_ba.clear()
                data_ba.extend(data_recv.encode('utf-8'))
                data_ba.append(0)
                l2cap_channel.send_pdu(data_ba)
                found, lines = await wait_for_shell_response(dut, data_recv)
                assert found is True

                logger.info(f"test l2cap server send data {data_send}")
                client_received.clear()
                await send_cmd_to_iut(shell, dut, f"l2cap_br send {L2CAP_CHAN_IUT_ID} {data_send} {str(hex(len(data_send)))[2:]}", None, wait=False)
                await asyncio.sleep(0.5)
                data_ba.clear()
                data_ba.extend(data_send.encode('utf-8'))
                recv_ba = bytearray()
                for data in client_received:
                    recv_ba.extend(data)
                assert data_ba == recv_ba

            await send_cmd_to_iut(shell, dut, f"l2cap_br disconnect {L2CAP_CHAN_IUT_ID}", f"Channel {L2CAP_CHAN_IUT_ID} disconnected")

            await bumble_acl_disconnect(shell, dut, device, connection)

class TestL2capBRServer:
        def test_l2cap_server_case_1(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect active authenticate and active l2cap disconnect"""
            logger.info(f'test_l2cap_server_case_1 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_1(hci, shell, dut, iut_address))
        def test_l2cap_server_case_2(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect active authenticate and passive l2cap disconnect"""
            logger.info(f'test_l2cap_server_case_2 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_2(hci, shell, dut, iut_address))
        def test_l2cap_server_case_3(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect active authenticate and active acl disconnect.l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_server_case_3 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_3(hci, shell, dut, iut_address))
        def test_l2cap_server_case_4(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test passive ACL connection,l2cap connect active authenticate and passive acl disconnect.l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_server_case_4 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_4(hci, shell, dut, iut_address))
        def test_l2cap_server_case_5(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect active authenticate and active l2cap disconnect"""
            logger.info(f'test_l2cap_server_case_5 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_5(hci, shell, dut, iut_address))
        def test_l2cap_server_case_6(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect active authenticate and passive l2cap disconnect"""
            logger.info(f'test_l2cap_server_case_6 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_6(hci, shell, dut, iut_address))
        def test_l2cap_server_case_7(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect active authenticate and active acl disconnect.l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_server_case_7 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_7(hci, shell, dut, iut_address))
        def test_l2cap_server_case_8(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test active ACL connection,l2cap connect active authenticate and passive acl disconnect.l2cap disconnect should be successfully."""
            logger.info(f'test_l2cap_server_case_8 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_8(hci, shell, dut, iut_address))
        def test_l2cap_server_case_9(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """ Test l2cap connection with max MTU(0xffff).But the max mtu which the stack supports is (CONFIG_BT_BUF_ACL_RX_SIZE - 4U = 196)."""
            logger.info(f'test_l2cap_server_case_9 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_9(hci, shell, dut, iut_address))
        def test_l2cap_server_case_10(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """ Test l2cap connection with min MTU(0x30)."""
            logger.info(f'test_l2cap_server_case_10 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_10(hci, shell, dut, iut_address))
        def test_l2cap_server_case_11(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """ Test l2cap connection with invaild PSM."""
            logger.info(f'test_l2cap_server_case_11 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_11(hci, shell, dut, iut_address))
        def test_l2cap_server_case_12(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Test l2cap multi_channel connection and data tranfer. Two PSM(0x1001, 0x1003) are registered in iut which is server"""
            logger.info(f'test_l2cap_server_case_12 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_12(hci, shell, dut, iut_address))
        def test_l2cap_server_case_13(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Stress Test. Repeat l2cap connect, disconnect operation"""
            logger.info(f'test_l2cap_server_case_13 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_13(hci, shell, dut, iut_address))
        def test_l2cap_server_case_14(self, shell: Shell, dut: DeviceAdapter, l2cap_br_dut): 
            """Stress Test. duplicate data transfer operation"""
            logger.info(f'test_l2cap_server_case_14 {l2cap_br_dut}')
            hci, iut_address = l2cap_br_dut
            asyncio.run(l2cap_server_case_14(hci, shell, dut, iut_address))
