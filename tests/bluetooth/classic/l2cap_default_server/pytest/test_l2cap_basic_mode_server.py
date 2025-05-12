import time

from conftest import (
    L2CAP_SERVER_PSM_BASIC,
    MODE_OPTION,
    logger,
)


def test_l2cap_basic_mode_TC01(client, server):
    L2CAP_CHAN_IUT_ID_BASIC = 0
    time.sleep(0.5)

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_BASIC}, mode_option = false')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} 0',
        f'psm {str(int(L2CAP_SERVER_PSM_BASIC))} mode_optional 0',
    )

    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', f'Connected', timeout=10)

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = basic, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} basic', f'It is basic mode'
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = basic, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} basic {MODE_OPTION}',
        r'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = ret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} ret',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = ret, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} ret {MODE_OPTION}',
        r'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = fc, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} fc',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = fc, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} fc {MODE_OPTION}',
        r'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = eret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} eret',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = eret, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} eret {MODE_OPTION}',
        r'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = stream, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} stream',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = stream, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} stream {MODE_OPTION}',
        r'It is basic mode',
    )

    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    server.iexpect('bt disconnect', r'Disconnected')


def test_l2cap_basic_mode_TC02(client, server):
    L2CAP_CHAN_IUT_ID_BASIC = 0
    time.sleep(0.5)

    logger.info(f'server register , psm = {L2CAP_SERVER_PSM_BASIC}, mode_option = true')
    server.iexpect(
        f'l2cap_br modify_mop {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} 1',
        f'psm {str(int(L2CAP_SERVER_PSM_BASIC))} mode_optional 1',
    )

    logger.info(f'acl connect {server.addr}')
    client.iexpect(f'br connect {server.addr}', r'Connected', timeout=10)
    time.sleep(0.5)

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = basic, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} basic', f'It is basic mode'
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = basic, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} basic {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = ret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} ret',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = ret, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} ret {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = fc, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} fc',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = fc, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} fc {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = eret, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} eret',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = eret, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} eret {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = stream, mode_option = false'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} stream',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    logger.info(
        f'client create l2cap. psm = {L2CAP_SERVER_PSM_BASIC}, mode = stream, mode_option = true'
    )
    client.iexpect(
        f'l2cap_br connect {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} stream {MODE_OPTION}',
        f'It is basic mode',
    )
    client.iexpect(
        f'l2cap_br disconnect {L2CAP_CHAN_IUT_ID_BASIC}',
        f'Channel {L2CAP_CHAN_IUT_ID_BASIC} disconnected',
    )

    server.iexpect('bt disconnect', r'Disconnected')
