# default
# mps = BT_L2CAP_STD_CONTROL_SIZE(2) + BT_L2CAP_RT_FC_SDU_LEN_SIZE(2)
# + payload_size + BT_L2CAP_FCS_SIZE(2)


import time

from conftest import (
    L2CAP_SERVER_PSM_RET,
    L2CAP_SERVER_PSM_FC,
    logger,
)

BT_L2CAP_MPS = 48
BT_L2CAP_STD_CONTROL_SIZE = 2
BT_L2CAP_RT_FC_SDU_LEN_SIZE = 2
BT_L2CAP_FCS_SIZE = 2

PAYLOAD_LEN = (
    BT_L2CAP_MPS - BT_L2CAP_STD_CONTROL_SIZE - BT_L2CAP_RT_FC_SDU_LEN_SIZE - BT_L2CAP_FCS_SIZE
)


def test_l2cap_ret_mode_TC05(client, server):
    L2CAP_CHAN_IUT_ID_RET = 0

    time.sleep(1)
    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', 'Connected', timeout=10)
    time.sleep(0.5)

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_RET}, mode = ret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_RET))[2:]} ret',
        'It is retransmission mode',
    )

    logger.info('TEST client send data to server')

    logger.info('client send data1, len < PAYLOAD_LEN')
    data1 = "this_is_data1"
    data_len = len(data1)
    assert data_len < PAYLOAD_LEN
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_RET} {data1} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response(f"{data1}")
    seg_count = 0
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
    assert seg_count == 1

    logger.info('client send data2, PAYLOAD_LEN< len < 2*PAYLOAD_LEN')
    data2 = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM1234567!@#"
    data_len = len(data2)
    assert data_len > PAYLOAD_LEN and data_len < 2 * PAYLOAD_LEN
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_RET} {data2} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response("!@#")
    seg_count = 0
    server_recv_data = ""
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
            server_recv_data += line.split('data:')[1]
    assert seg_count == 2 and server_recv_data == data2

    logger.info('TEST server send data to client')

    logger.info('client send data3, len < PAYLOAD_LEN')
    data3 = "this_is_server_send"
    data_len = len(data3)
    assert data_len < PAYLOAD_LEN
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_RET} {data3} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response(f"{data3}")
    seg_count = 0
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
    assert seg_count == 1

    logger.info('server send data4, PAYLOAD_LEN< len < 2*PAYLOAD_LEN')
    data4 = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM1234567][]"
    data_len = len(data4)
    assert data_len > PAYLOAD_LEN and data_len < 2 * PAYLOAD_LEN
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_RET} {data4} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response("][]")
    seg_count = 0
    server_recv_data = ""
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
            server_recv_data += line.split('data:')[1]
    assert seg_count == 2 and server_recv_data == data4

    server.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_RET}',
        f'Channel {L2CAP_CHAN_IUT_ID_RET} disconnected',
    )

    server.iexpect('bt disconnect', r'Disconnected')


def test_l2cap_fc_mode_TC05(client, server):
    L2CAP_CHAN_IUT_ID_FC = 0

    time.sleep(1)
    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', 'Connected',timeout=10)
    time.sleep(0.5)

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = fc, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc', 'It is flow control mode'
    )

    logger.info('TEST client send data to server')

    logger.info('client send data1, len < PAYLOAD_LEN')
    data1 = "this_is_data1"
    data_len = len(data1)
    assert data_len < PAYLOAD_LEN
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data1} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response(f"{data1}")
    seg_count = 0
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
    assert seg_count == 1

    logger.info('client send data2, PAYLOAD_LEN< len < BT_L2CAP_MPS < 2*PAYLOAD_LEN')
    data2 = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM1234567!@#"
    data_len = len(data2)
    assert data_len > PAYLOAD_LEN and data_len < 2 * PAYLOAD_LEN and data_len < BT_L2CAP_MPS
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data2} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response("!@#")
    seg_count = 0
    server_recv_data = ""
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
            server_recv_data += line.split('data:')[1]
    assert seg_count == 2 and server_recv_data == data2

    logger.info('TEST server send data to client')

    logger.info('client send data3, len < PAYLOAD_LEN')
    data3 = "this_is_server_send"
    data_len = len(data3)
    assert data_len < PAYLOAD_LEN
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data3} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response(f"{data3}")
    seg_count = 0
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
    assert seg_count == 1

    logger.info('server send data4, PAYLOAD_LEN< len < BT_L2CAP_MPS < 2*PAYLOAD_LEN')
    data4 = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM1234567][]"
    data_len = len(data4)
    assert data_len > PAYLOAD_LEN and data_len < 2 * PAYLOAD_LEN and data_len < BT_L2CAP_MPS
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data4} {str(hex(data_len))}')
    _, lines = server._wait_for_shell_response("][]")
    seg_count = 0
    server_recv_data = ""
    for line in lines:
        if "Incoming data:" in line:
            seg_count += 1
            server_recv_data += line.split('data:')[1]
    assert seg_count == 2 and server_recv_data == data4

    server.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}',
        f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected',
    )

    server.iexpect('bt disconnect', r'Disconnected')
