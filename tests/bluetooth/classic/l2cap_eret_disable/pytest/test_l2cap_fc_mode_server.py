import time

from conftest import (
    L2CAP_SERVER_PSM_FC,
    MODE_OPTION,
    logger,
)
MODE="fc"
# stack do not support eret. disable CONFIG_BT_L2CAP_ENH_RET in  prj.conf
def test_l2cap_fc_mode_TC01(client, server):
    L2CAP_CHAN_IUT_ID_FC = 0

    time.sleep(1)
    logger.info(f'server registered , psm = {L2CAP_SERVER_PSM_FC}, mode_option = false') 
    server.iexpect(f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0", wait = False)

    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', r'Connected')
    time.sleep(.5)

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = basic, mode_option = false')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} basic', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = basic, mode_option = true')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} basic {MODE_OPTION}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = false. stack do not support eret.',)
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} ret', f'It is retransmission mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = true. stack do not support eret')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} ret {MODE_OPTION}', f'It is retransmission mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = fc, mode_option = false. stack do not support eret.')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc', f'It is flow control mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = fc, mode_option = true. stack do not support eret.')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc {MODE_OPTION}', f'It is flow control mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = stream, mode_option = false')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} stream', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = stream, mode_option = true.')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} stream {MODE_OPTION}', f'It is flow control mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    client.iexpect('bt disconnect', r'Disconnected')

def test_l2cap_fc_mode_TC02(client, server):
    L2CAP_CHAN_IUT_ID_FC = 0

    time.sleep(1)
    logger.info(f'server registered , psm = {L2CAP_SERVER_PSM_FC}, mode_option = true') 
    server.iexpect(f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1', f"psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 1", wait = False)

    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected')
    time.sleep(.5)

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = basic, mode_option = false')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} basic', f'It is basic mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = basic, mode_option = true')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} basic {MODE_OPTION}', f'It is basic mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = false. stack do not support eret.',)
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} ret', f'It is retransmission mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = true. stack do not support eret')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} ret {MODE_OPTION}', f'It is retransmission mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = fc, mode_option = false. stack do not support eret.')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc', f'It is flow control mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = fc, mode_option = true. stack do not support eret.')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc {MODE_OPTION}', f'It is flow control mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')


    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = stream, mode_option = false')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} stream', f'It is streaming mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = stream, mode_option = true.')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} stream {MODE_OPTION}', f'It is streaming mode')
    client.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    server.iexpect('bt disconnect', r'Disconnected')

def test_l2cap_fc_mode_TC03(client, server):
    L2CAP_CHAN_IUT_ID_FC = 0

    time.sleep(1)
    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=5)
    time.sleep(.5)

    logger.info(f'server registered , psm = {L2CAP_SERVER_PSM_FC}, mode_option = false') 
    server.iexpect(f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0", wait = False)

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = false')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc', f'It is flow control mode')

    logger.info(f'set server appl status - idle, and client appl status = idle')
    server.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0", wait = False)
    client.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0", wait = False)

    logger.info(f'client send data1 to server, server recv data1 success')
    data1 = "server_recv_data"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data1}  {str(hex(len(data1)))[2:]}')
    server._wait_for_shell_response(f"{data1}")

    logger.info(f'set server appl status - busy')
    server.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 1", wait = False)

    logger.info(f'client send data2 to server, server recv data2 success')
    data2 = "server_recv_data2"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data2}  {str(hex(len(data2)))[2:]}')
    server._wait_for_shell_response(f"{data2}")

    logger.info(f'client send data3 to server, server recv data3 fail')
    data3 = "server_recv_data3"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data3}  {str(hex(len(data3)))[2:]}')
    server._wait_for_shell_response(f"to allocate buffer for SDU")

    logger.info(f'client send data4 to server, server recv data3 fail')
    data4 = "server_recv_data4"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data4}  {str(hex(len(data4)))[2:]}')
    server._wait_for_shell_response(f"to allocate buffer for SDU")

    logger.info(f'set server appl status - idle')
    server.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0", wait = False)

    logger.info(f'client send data5 to server, server recv data5 success')
    data5 = "server_recv_data5"
    client.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data5}  {str(hex(len(data5)))[2:]}')
    server._wait_for_shell_response(f"{data5}")

    server.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    server.iexpect('bt disconnect', r'Disconnected')

def test_l2cap_fc_mode_TC04(client, server):
    L2CAP_CHAN_IUT_ID_FC = 0

    time.sleep(1)
    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=5)
    time.sleep(.5)

    logger.info(f'server registered , psm = {L2CAP_SERVER_PSM_FC}, mode_option = false') 
    server.iexpect(f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} mode_optional 0", wait = False)

    logger.info(f'client create l2cap. psm = {L2CAP_SERVER_PSM_FC}, mode = ret, mode_option = false')
    client.iexpect(f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc', f'It is flow control mode')

    logger.info(f'set server appl status - idle, and client appl status = idle')
    server.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0", wait = False)
    client.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0", wait = False)

    logger.info(f'server send data1 to server, client recv data1 success')
    data1 = "server_send_data"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data1}  {str(hex(len(data1)))[2:]}')
    client._wait_for_shell_response(f"{data1}")

    logger.info(f'set client appl status - busy')
    client.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 1', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 1", wait = False)

    logger.info(f'server send data2 to server, client recv data2 success')
    data2 = "server_send_data2"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data2}  {str(hex(len(data2)))[2:]}')
    client._wait_for_shell_response(f"{data2}")

    logger.info(f'server send data3 to server, client recv data3 fail')
    data3 = "server_send_data3"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data3}  {str(hex(len(data3)))[2:]}')
    client._wait_for_shell_response(f"to allocate buffer for SDU")

    logger.info(f'server send data4 to server, client recv data3 fail')
    data4 = "server_send_data4"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data4}  {str(hex(len(data4)))[2:]}')
    client._wait_for_shell_response(f"to allocate buffer for SDU")

    logger.info(f'set client appl status - idle')
    client.iexpect(f'l2cap_br modify_appl_status {str(hex(L2CAP_SERVER_PSM_FC))[2:]} 0', f"psm {str(int(L2CAP_SERVER_PSM_FC))} appl status 0", wait = False)

    logger.info(f'server send data5 to server, client recv data5 success')
    data5 = "server_send_data5"
    server.exec_command(f'l2cap_br send {L2CAP_CHAN_IUT_ID_FC} {data5}  {str(hex(len(data5)))[2:]}')
    client._wait_for_shell_response(f"{data5}")

    server.iexpect(f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_FC}', f'Channel {L2CAP_CHAN_IUT_ID_FC} disconnected')

    server.iexpect('bt disconnect', r'Disconnected')
