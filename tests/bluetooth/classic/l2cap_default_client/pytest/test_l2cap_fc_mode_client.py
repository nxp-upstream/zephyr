import time

from conftest import (
    L2CAP_SERVER_PSM_BASIC,
    L2CAP_SERVER_PSM_RET,
    L2CAP_SERVER_PSM_FC,
    L2CAP_SERVER_PSM_ERET,
    L2CAP_SERVER_PSM_STREAM,
    MODE_OPTION,
    logger,
)

MODE = "fc"


def test_l2cap_fc_mode_client_TC01(client, server):
    L2CAP_CHAN_IUT_ID = 0
    time.sleep(1)

    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=10)

    logger.info(
        f'client create l2cap connection, mode = {MODE}, mode_option = false'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_BASIC},mode=basic, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_BASIC))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} {MODE}',
        f'Channel {L2CAP_CHAN_IUT_ID} disconnected',
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_BASIC},mode=basic,mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_BASIC))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} {MODE}',
        f'Channel {L2CAP_CHAN_IUT_ID} disconnected',
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_RET}, mode=ret,mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_RET))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_RET))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_RET))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_RET},mode=ret, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_RET))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_RET))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_RET))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_FC},mode=fc, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_FC},mode=fc, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_ERET},mode=eret, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_ERET))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_ERET},mode=eret, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_ERET))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(
        f'server register , psm = {L2CAP_SERVER_PSM_STREAM},mode=stream, mode_option = false'
    )
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_STREAM))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} {MODE}',
        f'Channel {L2CAP_CHAN_IUT_ID} disconnected',
    )

    logger.info(
        f'server register , psm = {L2CAP_SERVER_PSM_STREAM},mode=stream, mode_option = true'
    )
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_STREAM))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} {MODE}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    client.iexpect('bt disconnect', r'Disconnected')


def test_l2cap_fc_mode_client_TC02(client, server):
    L2CAP_CHAN_IUT_ID = 0
    time.sleep(1)

    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=10)

    logger.info(
        f'client create l2cap connection, mode = {MODE}, mode_option = true'
    )

    logger.info(f'server register,psm = {L2CAP_SERVER_PSM_BASIC},mode=basic, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_BASIC))} mode_optional 0',
    )

    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} {MODE} {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_BASIC},mode=basic,mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_BASIC))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} {MODE} {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_RET}, mode=ret,mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_RET))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_RET))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_RET))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_RET},mode=ret, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_RET))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_RET))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_RET))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_FC},mode=fc, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_FC},mode=fc, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_ERET},mode=eret, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_ERET))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_ERET},mode=eret, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_ERET))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(
        f'server register , psm = {L2CAP_SERVER_PSM_STREAM},mode=stream, mode_option = false'
    )
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_STREAM))} mode_optional 0',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} {MODE} {MODE_OPTION}',
        f'It is streaming mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    logger.info(
        f'server register , psm = {L2CAP_SERVER_PSM_STREAM},mode=stream, mode_option = true'
    )
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_STREAM))} mode_optional 1',
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} {MODE} {MODE_OPTION}',
        f'It is enhance retransmission mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )

    client.iexpect('bt disconnect', r'Disconnected')


def test_l2cap_fc_mode_client_TC03(client, server):
    L2CAP_CHAN_IUT_ID = 0

    time.sleep(1)
    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=10)
    time.sleep(0.5)

    logger.info(f'server registered , psm = {L2CAP_SERVER_PSM_FC}, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0",
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} ret',
        f'It is enhance retransmission mode',
    )

    logger.info(f'set server and client appl status = idle')
    server.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0",
    )
    client.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0",
    )

    logger.info(f'client send data1, server recv data1 and result is successful')
    data1 = "client_send_data"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data1}  {str(hex(len(data1)))[2:]}')
    server._wait_for_shell_response(f"{data1}")

    logger.info(f'set l2cap server busy status')
    server.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 1",
    )

    logger.info(f'client send data2, server recv data2 and result is successful')
    data2 = "client_send_data2"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data2}  {str(hex(len(data2)))[2:]}')
    server._wait_for_shell_response(f"{data2}")

    logger.info(f'client send data3, server recv data3 and result is fail')
    data3 = "client_send_data3"
    client.iexpect(
        f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data3}  {str(hex(len(data3)))[2:]}',
        f"RNR p=0 f=0 on chan",
    )
    server._wait_for_shell_response(f"to allocate buffer for SDU")

    logger.info(f'set l2cap server idle status')
    server.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0",
    )
    server._wait_for_shell_response(f"{data3}", timeout=5)
    client._wait_for_shell_response(r'Retransmission I-frame', timeout=5)

    logger.info(f'client send data4, server recv data4 and result is successful')
    data4 = "client_send_data4"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data4} {str(hex(len(data4)))[2:]}')
    server._wait_for_shell_response(f"{data4}", timeout=5)

    server.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )
    client.iexpect('bt disconnect', r'Disconnected')


def test_l2cap_fc_mode_client_TC04(client, server):
    L2CAP_CHAN_IUT_ID = 0

    time.sleep(1)
    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=10)
    time.sleep(0.5)

    logger.info(f'server registered , psm = {L2CAP_SERVER_PSM_FC}, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0",
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} ret',
        f'It is enhance retransmission mode',
    )

    logger.info(f'set server and client appl status = idle')
    server.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0",
    )
    client.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0",
    )

    logger.info(f'server send data1, client recv data1 and result is successful')
    data1 = "client_recv_data"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data1}  {str(hex(len(data1)))[2:]}')
    client._wait_for_shell_response(f"{data1}")

    logger.info(f'set l2cap client busy status')
    client.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 1",
    )

    logger.info(f'server send data2, client recv data2 and result is successful')
    data2 = "client_recv_data2"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data2}  {str(hex(len(data2)))[2:]}')
    client._wait_for_shell_response(f"{data2}")

    logger.info(f'server send data3, client recv data3 and result is fail')
    data3 = "client_recv_data3"
    server.iexpect(
        f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data3}  {str(hex(len(data3)))[2:]}',
        f"RNR p=0 f=0 on chan",
    )
    client._wait_for_shell_response(f"to allocate buffer for SDU")

    logger.info(f'set l2cap client idle status')
    client.iexpect(
        f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0',
        f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0",
    )
    client._wait_for_shell_response(f"{data3}", timeout=5)
    server._wait_for_shell_response(r'Retransmission I-frame', timeout=5)

    logger.info(f'server send data4, client recv data4 and result is successful')
    data4 = "client_recv_data4"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID} {data4} {str(hex(len(data4)))[2:]}')
    client._wait_for_shell_response(f"{data4}", timeout=5)

    server.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID}', f'Channel {L2CAP_CHAN_IUT_ID} disconnected'
    )
    client.iexpect('bt disconnect', r'Disconnected')