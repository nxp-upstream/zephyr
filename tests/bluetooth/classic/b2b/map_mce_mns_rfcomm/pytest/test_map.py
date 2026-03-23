# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

import logging
import re
import time

logger = logging.getLogger(__name__)

BT_OBEX_VERSION = 0x10
BT_GOEP_MTU = 261


class OBEX_HDR:
    NAME = 0x01


class RspCode:
    CONTINUE = 0x90
    SUCCESS = 0xA0
    BAD_REQ = 0xC0
    FORBIDDEN = 0xC3
    NOT_FOUND = 0xC4
    NOT_ACCEPT = 0xC6


rsp_code_str = {
    RspCode.CONTINUE: "Continue",
    RspCode.SUCCESS: "Success",
    RspCode.BAD_REQ: "Bad Request - server couldn't understand request",
    RspCode.FORBIDDEN: "Forbidden - operation is understood but refused",
    RspCode.NOT_FOUND: "Not Found",
    RspCode.NOT_ACCEPT: "Not Acceptable",
}


def br_pre(init, rsp):
    init.exec_command('br clear all')
    rsp.exec_command('br clear all')
    init.exec_command('bt clear all')
    rsp.exec_command('bt clear all')


def _br_connect(init, rsp):
    # BR connection
    init.exec_command(f'br connect {rsp.pub_addr}')
    found, _ = init.wait_for_shell_response(f'Connected: {rsp.pub_addr}')
    assert found is True
    found, _ = rsp.wait_for_shell_response(f'Connected: {init.pub_addr}')
    assert found is True


def br_connect(init, rsp):
    count = 3
    while count > 0:
        try:
            _br_connect(init, rsp)
            break
        except Exception:
            time.sleep(5)
            count -= 1
            if count > 0:
                logger.info('Retry BR connection')
            continue


def br_disconnect(init, rsp):
    init.exec_command(f'bt disconnect {rsp.pub_addr}')
    found, _ = init.wait_for_shell_response(f'Disconnected: {rsp.pub_addr}')
    assert found is True
    found, _ = rsp.wait_for_shell_response(f'Disconnected: {init.pub_addr}')
    assert found is True


def br_security(init, rsp):
    # BR security

    dut_sec = 2  # Default security level 2
    init.exec_command(f'bt security {dut_sec}')
    expected = [f"Security changed: {rsp.pub_addr} level {dut_sec}"]
    expected.append(f"Bonded with {rsp.pub_addr}")
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    expected = [f"Security changed: {init.pub_addr} level {dut_sec}"]
    expected.append(f"Bonded with {init.pub_addr}")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def mns_rfcomm_connect(init, rsp):
    channel = rsp.channel
    supported_features = rsp.supported_features

    init.exec_command(f'test_map mse mns rfcomm_connect {channel:02X} {supported_features:08X}')
    expected = rf"MSE MNS RFCOMM connected: \w+, addr: {rsp.pub_addr}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True
    expected = rf"MCE MNS RFCOMM connected: \w+, addr: {init.pub_addr}"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def mns_rfcomm_connect_fail(init, rsp):
    channel = 0x1E  # RFCOMM_CHANNEL_END to trigger connection failure
    supported_features = rsp.supported_features

    lines = init.exec_command(
        f'test_map mse mns rfcomm_connect {channel:02X} {supported_features:08X}'
    )
    expected = "MSE MNS RFCOMM disconnected"
    found = init.check_shell_response(lines, expected)
    if found is False:
        found, _ = init.wait_for_shell_response(expected)
        assert found is True


def mns_rfcomm_disconnect(init, rsp):
    init.exec_command('test_map mse mns rfcomm_disconnect')
    found, _ = init.wait_for_shell_response("MSE MNS RFCOMM disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MCE MNS RFCOMM disconnected:")
    assert found is True


def mns_l2cap_connect(init, rsp):
    psm = rsp.psm
    supported_features = rsp.supported_features

    init.exec_command(f'test_map mse mns l2cap_connect {psm:04X} {supported_features:08X}')
    expected = rf"MSE MNS L2CAP connected: \w+, addr: {rsp.pub_addr}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True
    expected = rf"MCE MNS L2CAP connected: \w+, addr: {init.pub_addr}"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def mns_l2cap_connect_fail(init, rsp):
    psm = rsp.psm + 0x1000  # Invalid PSM to trigger connection failure
    supported_features = rsp.supported_features

    lines = init.exec_command(f'test_map mse mns l2cap_connect {psm:04X} {supported_features:08X}')
    expected = "MSE MNS L2CAP disconnected"
    found = init.check_shell_response(lines, expected)
    if found is False:
        found, _ = init.wait_for_shell_response(expected)
        assert found is True


def mns_l2cap_disconnect(init, rsp):
    init.exec_command('test_map mse mns l2cap_disconnect')
    found, _ = init.wait_for_shell_response("MSE MNS L2CAP disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MCE MNS L2CAP disconnected:")
    assert found is True


def mce_mns_l2cap_disconnect(init, rsp):
    init.exec_command('test_map mce mns l2cap_disconnect')
    found, _ = init.wait_for_shell_response("MCE MNS L2CAP disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MSE MNS L2CAP disconnected:")
    assert found is True


def mns_connect(init, rsp, rsp_code):
    init.exec_command('test_map mse mns connect')
    expected = (
        rf"MCE MNS \w+ conn req, "
        rf"version {BT_OBEX_VERSION:02x}, mopl"
    )
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    cmd = "test_map mce mns connect "
    if rsp_code == RspCode.SUCCESS:
        cmd += 'success'
    elif rsp_code == RspCode.CONTINUE:
        cmd += 'continue'
    else:
        cmd += f'error {rsp_code:X}'
    rsp.exec_command(cmd)

    expected = rf"MSE MNS \w+ conn rsp, rsp_code {rsp_code_str[rsp_code]}"
    expected += rf", version {BT_OBEX_VERSION:02x}, mopl"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mns_disconnect(init, rsp, rsp_code):
    init.exec_command('test_map mse mns disconnect')
    expected = r"MCE MNS \w+ disconn req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    cmd = "test_map mce mns disconnect "
    if rsp_code == RspCode.SUCCESS:
        cmd += 'success'
    elif rsp_code == RspCode.CONTINUE:
        cmd += 'continue'
    else:
        cmd += f'error {rsp_code:X}'
    rsp.exec_command(cmd)

    expected = rf"MSE MNS \w+ disconn rsp, rsp_code {rsp_code_str[rsp_code]}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mns_put_cmd_initiate(init, rsp, cmd):
    """Initiate a PUT command and wait for initial Continue response."""
    init.exec_command(f'test_map mse mns {cmd}')
    expected = [rf"MCE MNS \w+ {cmd} req, final false"]
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mce mns {cmd} noerror')
    expected = rf"MSE MNS \w+ {cmd} rsp, rsp_code Continue"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mns_put_cmd_complete(init, rsp, cmd):
    """Complete a PUT command by continuing until Success response."""
    rsp_code = 'Continue'
    final = 'false'

    while True:
        init.exec_command(f'test_map mse mns {cmd}')
        expected = [rf"MCE MNS \w+ {cmd} req, final (?P<final>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MNS \w+ {cmd} req, final (?P<final>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        rsp.exec_command(f'test_map mce mns {cmd} noerror')
        expected = rf"MSE MNS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
        found, lines = init.wait_for_shell_response(expected)
        assert found is True

        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                rsp_code = searched.group("rsp_code")
                break

        if rsp_code != 'Continue' or final == 'true':
            break

    assert rsp_code == 'Success' and final == 'true'


def mns_put_cmd(init, rsp, cmd):
    mns_put_cmd_complete(init, rsp, cmd)


def mns_put_cmd_continue(init, rsp, cmd):
    mns_put_cmd_initiate(init, rsp, cmd)
    mns_put_cmd_complete(init, rsp, cmd)


def mns_put_cmd_fail(init, rsp, cmd, rsp_code):
    init.exec_command(f'test_map mse mns {cmd}')
    expected = [rf"MCE MNS \w+ {cmd} req, final \w+"]
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mce mns {cmd} error {rsp_code:X}')
    expected = rf"MSE MNS \w+ {cmd} rsp, rsp_code {rsp_code_str[rsp_code]}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mns_put_cmd_srm(init, rsp, cmd):
    rsp_code = 'Continue'
    final = 'false'

    init.exec_command(f'test_map mse mns {cmd}')
    expected = [rf"MCE MNS \w+ {cmd} req, final \w+"]
    expected.append("OBEX SRM: 01")
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mce mns {cmd} noerror')
    expected = [rf"MSE MNS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    found, lines = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        init.exec_command(f'test_map mse mns {cmd}')
        expected = [rf"MCE MNS \w+ {cmd} req, final (?P<final>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MNS \w+ {cmd} req, final (?P<final>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        if final == 'true':
            break

    assert final == 'true'

    rsp.exec_command(f'test_map mce mns {cmd} noerror')
    expected = rf"MSE MNS \w+ {cmd} rsp, rsp_code Success"
    found, lines = init.wait_for_shell_response(expected)
    assert found is True


def mns_put_cmd_srm_param(init, rsp, cmd):
    rsp_code = 'Continue'
    final = 'false'

    init.exec_command(f'test_map mse mns {cmd}')
    expected = [rf"MCE MNS \w+ {cmd} req, final \w+"]
    expected.append("OBEX SRM: 01")
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mce mns {cmd} noerror srmp')
    expected = [rf"MSE MNS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    expected.append("OBEX SRMP: 01")
    found, lines = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        init.exec_command(f'test_map mse mns {cmd}')
        expected = [rf"MCE MNS \w+ {cmd} req, final (?P<final>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MNS \w+ {cmd} req, final (?P<final>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        if final == 'true':
            break

    assert final == 'true'

    rsp.exec_command(f'test_map mce mns {cmd} noerror')
    expected = rf"MSE MNS \w+ {cmd} rsp, rsp_code Success"
    found, lines = init.wait_for_shell_response(expected)
    assert found is True


def mns_put_cmd_abort(init, rsp, cmd):
    mns_put_cmd_initiate(init, rsp, cmd)

    init.exec_command('test_map mse mns abort')
    expected = r"MCE MNS \w+ abort req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command('test_map mce mns abort success')
    expected = r"MSE MNS \w+ abort rsp, rsp_code Success"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mns_put_cmd_abort_fail(init, rsp, cmd, rsp_code):
    mns_put_cmd_initiate(init, rsp, cmd)

    init.exec_command('test_map mse mns abort')
    expected = r"MCE MNS \w+ abort req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mce mns abort error {rsp_code:X}')
    expected = r"MSE MNS L2CAP disconnected: \w+"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    expected = r"MCE MNS L2CAP disconnected: \w+"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def test_br_map_mce_mns_rfcomm_connect_success(client, server):
    """Verify that MCE MNS can successfully accept an incoming RFCOMM connection from MSE."""
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    br_disconnect(server, client)


def test_br_map_mce_mns_rfcomm_disconnect_success(client, server):
    """Verify that MCE MNS can successfully disconnect an RFCOMM connection."""
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    mns_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mce_mns_obex_connect_success(client, server):
    """Verify that MCE MNS can successfully accept an OBEX CONNECT request
    and respond with success."""
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    mns_connect(server, client, RspCode.SUCCESS)
    mns_disconnect(server, client, RspCode.SUCCESS)
    mns_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mce_mns_obex_connect_error(client, server):
    """Verify that MCE MNS can reject an OBEX CONNECT request with an error response."""
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    mns_connect(server, client, RspCode.FORBIDDEN)
    mns_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mce_mns_sendevent_success(client, server):
    """
    Verify that MCE MNS can successfully receive a complete SendEvent request
    and respond with success.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    mns_connect(server, client, RspCode.SUCCESS)
    mns_put_cmd(server, client, 'send_event')
    mns_disconnect(server, client, RspCode.SUCCESS)
    mns_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mce_mns_sendevent_error(client, server):
    """
    Verify that MCE MNS can respond to a SendEvent request with an error code.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    mns_connect(server, client, RspCode.SUCCESS)
    mns_put_cmd_fail(server, client, 'send_event', RspCode.BAD_REQ)
    mns_disconnect(server, client, RspCode.SUCCESS)
    mns_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mce_mns_sendevent_continue(client, server):
    """
    Verify that MCE MNS can handle multi-packet SendEvent requests by responding with CONTINUE.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mns_rfcomm_connect(server, client)
    mns_connect(server, client, RspCode.SUCCESS)
    mns_put_cmd_continue(server, client, 'send_event')
    mns_disconnect(server, client, RspCode.SUCCESS)
    mns_rfcomm_disconnect(server, client)
    br_disconnect(server, client)
