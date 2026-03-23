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


def mas_rfcomm_connect(init, rsp):
    channel = rsp.channel
    instance_id = rsp.instance_id
    supported_features = rsp.supported_features

    init.exec_command(
        f'test_map mce mas rfcomm_connect {channel:02X} {instance_id} {supported_features:08X}'
    )
    expected = rf"MCE MAS RFCOMM connected: \w+, addr: {rsp.pub_addr}, instance_id: {instance_id}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True
    expected = rf"MSE MAS RFCOMM connected: \w+, addr: {init.pub_addr}, instance_id: {instance_id}"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def mas_rfcomm_connect_fail(init, rsp):
    channel = 0x1E  # RFCOMM_CHANNEL_END to trigger connection failure
    instance_id = rsp.instance_id
    supported_features = rsp.supported_features

    lines = init.exec_command(
        f'test_map mce mas rfcomm_connect {channel:02X} {instance_id} {supported_features:08X}'
    )
    expected = "MCE MAS RFCOMM disconnected"
    found = init.check_shell_response(lines, expected)
    if found is False:
        found, _ = init.wait_for_shell_response(expected)
        assert found is True


def mas_rfcomm_disconnect(init, rsp):
    init.exec_command('test_map mce mas rfcomm_disconnect')
    found, _ = init.wait_for_shell_response("MCE MAS RFCOMM disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MSE MAS RFCOMM disconnected:")
    assert found is True


def mse_mas_rfcomm_disconnect(init, rsp):
    init.exec_command('test_map mse mas rfcomm_disconnect')
    found, _ = init.wait_for_shell_response("MSE MAS RFCOMM disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MCE MAS RFCOMM disconnected:")
    assert found is True


def mas_l2cap_connect(init, rsp):
    psm = rsp.psm
    instance_id = rsp.instance_id
    supported_features = rsp.supported_features

    init.exec_command(
        f'test_map mce mas l2cap_connect {psm:04X} {instance_id} {supported_features:08X}'
    )
    expected = rf"MCE MAS L2CAP connected: \w+, addr: {rsp.pub_addr}, instance_id: {instance_id}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True
    expected = rf"MSE MAS L2CAP connected: \w+, addr: {init.pub_addr}, instance_id: {instance_id}"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def mas_l2cap_connect_fail(init, rsp):
    psm = rsp.psm + 0x1000  # Invalid PSM to trigger connection failure
    instance_id = rsp.instance_id
    supported_features = rsp.supported_features

    lines = init.exec_command(
        f'test_map mce mas l2cap_connect {psm:04X} {instance_id} {supported_features:08X}'
    )
    expected = "MCE MAS L2CAP disconnected"
    found = init.check_shell_response(lines, expected)
    if found is False:
        found, _ = init.wait_for_shell_response(expected)
        assert found is True


def mas_l2cap_disconnect(init, rsp):
    init.exec_command('test_map mce mas l2cap_disconnect')
    found, _ = init.wait_for_shell_response("MCE MAS L2CAP disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MSE MAS L2CAP disconnected:")
    assert found is True


def mse_mas_l2cap_disconnect(init, rsp):
    init.exec_command('test_map mse mas l2cap_disconnect')
    found, _ = init.wait_for_shell_response("MSE MAS L2CAP disconnected:")
    assert found is True
    found, _ = rsp.wait_for_shell_response("MCE MAS L2CAP disconnected:")
    assert found is True


def mas_connect(init, rsp, rsp_code):
    init.exec_command('test_map mce mas connect')
    expected = (
        rf"MSE MAS \w+ conn req, "
        rf"version {BT_OBEX_VERSION:02x}, mopl"
    )
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    cmd = "test_map mse mas connect "
    if rsp_code == RspCode.SUCCESS:
        cmd += 'success'
    elif rsp_code == RspCode.CONTINUE:
        cmd += 'continue'
    else:
        cmd += f'error {rsp_code:X}'
    rsp.exec_command(cmd)

    expected = rf"MCE MAS \w+ conn rsp, rsp_code {rsp_code_str[rsp_code]}"
    expected += rf", version {BT_OBEX_VERSION:02x}, mopl"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_disconnect(init, rsp, rsp_code):
    init.exec_command('test_map mce mas disconnect')
    expected = r"MSE MAS \w+ disconn req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    cmd = "test_map mse mas disconnect "
    if rsp_code == RspCode.SUCCESS:
        cmd += 'success'
    elif rsp_code == RspCode.CONTINUE:
        cmd += 'continue'
    else:
        cmd += f'error {rsp_code:X}'
    rsp.exec_command(cmd)

    expected = rf"MCE MAS \w+ disconn rsp, rsp_code {rsp_code_str[rsp_code]}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_get_cmd_initiate(init, rsp, cmd):
    """Initiate a MAP command and wait for initial Continue response."""
    init.exec_command(f'test_map mce mas {cmd}')
    expected = rf"MSE MAS \w+ {cmd} req, final true"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code Continue"]
    expected.append(r"OBEX BODY: .*")
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_get_cmd_complete(init, rsp, cmd):
    """Complete a MAP command by continuing until Success response."""
    rsp_code = 'Continue'

    while True:
        init.exec_command(f'test_map mce mas {cmd}')
        expected = rf"MSE MAS \w+ {cmd} req, final true"
        found, _ = rsp.wait_for_shell_response(expected)
        assert found is True

        rsp.exec_command(f'test_map mse mas {cmd} noerror')
        expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = init.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                rsp_code = searched.group("rsp_code")
                break

        if rsp_code != 'Continue':
            break

    assert rsp_code == 'Success'


def mas_get_cmd(init, rsp, cmd):
    mas_get_cmd_complete(init, rsp, cmd)


def mas_get_cmd_continue(init, rsp, cmd):
    mas_get_cmd_initiate(init, rsp, cmd)
    mas_get_cmd_complete(init, rsp, cmd)


def mas_get_cmd_fail(init, rsp, cmd, rsp_code):
    init.exec_command(f'test_map mce mas {cmd}')
    expected = rf"MSE MAS \w+ {cmd} req, final true"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} error {rsp_code:X}')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code_str[rsp_code]}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_get_cmd_final(init, rsp, cmd):
    rsp_code = 'Continue'
    final = 'false'

    # Request phase
    init.exec_command(f'test_map mce mas {cmd} chunked_req')
    expected = rf"MSE MAS \w+ {cmd} req, final false"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        init.exec_command(f'test_map mce mas {cmd}')
        expected = rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        rsp.exec_command(f'test_map mse mas {cmd} noerror')
        expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
        found, lines = init.wait_for_shell_response(expected)
        assert found is True

        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                rsp_code = searched.group("rsp_code")
                break

        if rsp_code != 'Continue' or final == 'true':
            break

    assert final == 'true'

    if rsp_code == 'Success':
        expected = r"OBEX BODY: .*"
        found = init.check_shell_response(lines, expected)
        if not found:
            found, _ = init.wait_for_shell_response(expected)
            assert found is True
        return

    assert rsp_code == 'Continue'

    # Response phase
    mas_get_cmd_complete(init, rsp, cmd)


def mas_get_cmd_final_fail(init, rsp, cmd, rsp_code):
    init.exec_command(f'test_map mce mas {cmd} chunked_req')
    expected = rf"MSE MAS \w+ {cmd} req, final false"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} error {rsp_code:X}')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code_str[rsp_code]}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_get_cmd_srm(init, rsp, cmd):
    rsp_code = 'Continue'

    init.exec_command(f'test_map mce mas {cmd}')
    expected = [rf"MSE MAS \w+ {cmd} req, final true"]
    expected.append("OBEX SRM: 01")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    expected.append(r"OBEX BODY: .*")
    found, lines = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        rsp.exec_command(f'test_map mse mas {cmd} noerror')
        expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = init.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                rsp_code = searched.group("rsp_code")
                break

        if rsp_code != 'Continue':
            break

    assert rsp_code == 'Success'


def mas_get_cmd_final_srm(init, rsp, cmd):
    rsp_code = 'Continue'
    final = 'false'

    # Request phase
    init.exec_command(f'test_map mce mas {cmd} chunked_req')
    expected = [rf"MSE MAS \w+ {cmd} req, final false"]
    expected.append("OBEX SRM: 01")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        init.exec_command(f'test_map mce mas {cmd}')
        expected = rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        if final == 'true':
            break

    assert final == 'true'

    # Response phase
    while True:
        rsp.exec_command(f'test_map mse mas {cmd} noerror')
        expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = init.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                rsp_code = searched.group("rsp_code")
                break

        if rsp_code != 'Continue':
            break

    assert rsp_code == 'Success'


def mas_get_cmd_srm_param(init, rsp, cmd):
    rsp_code = 'Continue'

    init.exec_command(f'test_map mce mas {cmd} srmp')
    expected = [rf"MSE MAS \w+ {cmd} req, final true"]
    expected.append("OBEX SRM: 01")
    expected.append("OBEX SRMP: 01")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror srmp')
    expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    expected.append("OBEX SRMP: 01")
    expected.append(r"OBEX BODY: .*")
    found, lines = init.wait_for_shell_response(expected)
    assert found is True

    init.exec_command(f'test_map mce mas {cmd}')
    expected = rf"MSE MAS \w+ {cmd} req, final true"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    while True:
        rsp.exec_command(f'test_map mse mas {cmd} noerror')
        expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = init.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                rsp_code = searched.group("rsp_code")
                break

        if rsp_code != 'Continue':
            break

    assert rsp_code == 'Success'


def mas_get_cmd_abort(init, rsp, cmd):
    mas_get_cmd_initiate(init, rsp, cmd)

    init.exec_command('test_map mce mas abort')
    expected = r"MSE MAS \w+ abort req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command('test_map mse mas abort success')
    expected = r"MCE MAS \w+ abort rsp, rsp_code Success"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_get_cmd_abort_fail(init, rsp, cmd, rsp_code):
    mas_get_cmd_initiate(init, rsp, cmd)

    init.exec_command('test_map mce mas abort')
    expected = r"MSE MAS \w+ abort req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas abort error {rsp_code:X}')
    expected = r"MCE MAS L2CAP disconnected: \w+"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    expected = r"MSE MAS L2CAP disconnected: \w+"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def mas_put_cmd_initiate(init, rsp, cmd):
    """Initiate a PUT command and wait for initial Continue response."""
    init.exec_command(f'test_map mce mas {cmd}')
    expected = [rf"MSE MAS \w+ {cmd} req, final false"]
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code Continue"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_put_cmd_complete(init, rsp, cmd):
    """Complete a PUT command by continuing until Success response."""
    rsp_code = 'Continue'
    final = 'false'

    while True:
        init.exec_command(f'test_map mce mas {cmd}')
        expected = [rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        rsp.exec_command(f'test_map mse mas {cmd} noerror')
        expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code (?P<rsp_code>\w+)"
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


def mas_put_cmd(init, rsp, cmd):
    mas_put_cmd_complete(init, rsp, cmd)


def mas_put_cmd_continue(init, rsp, cmd):
    mas_put_cmd_initiate(init, rsp, cmd)
    mas_put_cmd_complete(init, rsp, cmd)


def mas_put_cmd_fail(init, rsp, cmd, rsp_code):
    init.exec_command(f'test_map mce mas {cmd}')
    expected = [rf"MSE MAS \w+ {cmd} req, final \w+"]
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} error {rsp_code:X}')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code_str[rsp_code]}"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_put_cmd_srm(init, rsp, cmd):
    rsp_code = 'Continue'
    final = 'false'

    init.exec_command(f'test_map mce mas {cmd}')
    expected = [rf"MSE MAS \w+ {cmd} req, final false"]
    expected.append("OBEX SRM: 01")
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        init.exec_command(f'test_map mce mas {cmd}')
        expected = [rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        if final == 'true':
            break

    assert final == 'true'

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code Success"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_put_cmd_srm_param(init, rsp, cmd):
    rsp_code = 'Continue'
    final = 'false'

    init.exec_command(f'test_map mce mas {cmd}')
    expected = [rf"MSE MAS \w+ {cmd} req, final false"]
    expected.append("OBEX SRM: 01")
    expected.append(r"OBEX BODY: .*")
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas {cmd} noerror srmp')
    expected = [rf"MCE MAS \w+ {cmd} rsp, rsp_code {rsp_code}"]
    expected.append("OBEX SRM: 01")
    expected.append("OBEX SRMP: 01")
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    while True:
        init.exec_command(f'test_map mce mas {cmd}')
        expected = [rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"]
        expected.append(r"OBEX BODY: .*")
        found, lines = rsp.wait_for_shell_response(expected)
        assert found is True

        expected = rf"MSE MAS \w+ {cmd} req, final (?P<final>\w+)"
        for line in lines:
            searched = re.search(expected, line)
            if searched is not None:
                final = searched.group("final")
                break

        if final == 'true':
            break

    assert final == 'true'

    rsp.exec_command(f'test_map mse mas {cmd} noerror')
    expected = rf"MCE MAS \w+ {cmd} rsp, rsp_code Success"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_put_cmd_abort(init, rsp, cmd):
    mas_put_cmd_initiate(init, rsp, cmd)

    init.exec_command('test_map mce mas abort')
    expected = r"MSE MAS \w+ abort req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command('test_map mse mas abort success')
    expected = r"MCE MAS \w+ abort rsp, rsp_code Success"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True


def mas_put_cmd_abort_fail(init, rsp, cmd, rsp_code):
    mas_put_cmd_initiate(init, rsp, cmd)

    init.exec_command('test_map mce mas abort')
    expected = r"MSE MAS \w+ abort req"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True

    rsp.exec_command(f'test_map mse mas abort error {rsp_code:X}')
    expected = r"MCE MAS L2CAP disconnected: \w+"
    found, _ = init.wait_for_shell_response(expected)
    assert found is True

    expected = r"MSE MAS L2CAP disconnected: \w+"
    found, _ = rsp.wait_for_shell_response(expected)
    assert found is True


def test_br_map_mse_rfcomm_connect_success(client, server):
    """
    BR_MAP_MSE_RFCOMM_CONNECT_SUCCESS

    Verify that the MSE can successfully accept an RFCOMM connection from MCE.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_rfcomm_connect_failure(client, server):
    """
    BR_MAP_MSE_RFCOMM_CONNECT_FAILURE

    Verify that the MSE handles RFCOMM connection rejection correctly.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect_fail(server, client)
    br_disconnect(server, client)


def test_br_map_mse_rfcomm_disconnect_success(client, server):
    """
    BR_MAP_MSE_RFCOMM_DISCONNECT_SUCCESS

    Verify that the MSE can successfully disconnect an RFCOMM connection.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mse_mas_rfcomm_disconnect(client, server)
    br_disconnect(server, client)


def test_br_map_mse_rfcomm_disconnect_failure(client, server):
    """
    BR_MAP_MSE_RFCOMM_DISCONNECT_FAILURE

    Verify that the MSE handles RFCOMM disconnection errors correctly.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)

    # Try to disconnect without establishing connection first
    lines = server.exec_command('test_map mse mas rfcomm_disconnect')
    expected = "No default MSE MAS conn available"
    found = server.check_shell_response(lines, expected)
    if found is False:
        found, _ = server.wait_for_shell_response(expected)
        assert found is True

    br_disconnect(server, client)


def test_br_map_mse_get_folder_listing_final_success(client, server):
    """
    BR_MAP_MSE_GET_FOLDER_LISTING_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetFolderListing request
    with final bit set in a single response.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd(server, client, 'get_folder_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_folder_listing_non_final_success(client, server):
    """
    BR_MAP_MSE_GET_FOLDER_LISTING_NON_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetFolderListing request
    with final bit not set (multi-packet transfer).
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final(server, client, 'get_folder_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_folder_listing_final_failure(client, server):
    """
    BR_MAP_MSE_GET_FOLDER_LISTING_FINAL_FAILURE

    Verify that the MSE handles GetFolderListing errors correctly when final bit is set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_fail(server, client, 'get_folder_listing', RspCode.FORBIDDEN)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_folder_listing_non_final_failure(client, server):
    """
    BR_MAP_MSE_GET_FOLDER_LISTING_NON_FINAL_FAILURE

    Verify that the MSE handles GetFolderListing errors correctly when final bit is not set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final_fail(server, client, 'get_folder_listing', RspCode.FORBIDDEN)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_folder_listing_continue_response(client, server):
    """
    BR_MAP_MSE_GET_FOLDER_LISTING_CONTINUE_RESPONSE

    Verify that the MSE correctly handles Continue (0x90) response during GetFolderListing.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_continue(server, client, 'get_folder_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_listing_final_success(client, server):
    """
    BR_MAP_MSE_GET_MSG_LISTING_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetMessagesListing request
    with final bit set in a single response.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd(server, client, 'get_msg_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_listing_non_final_success(client, server):
    """
    BR_MAP_MSE_GET_MSG_LISTING_NON_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetMessagesListing request
    with final bit not set (multi-packet transfer).
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final(server, client, 'get_msg_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_listing_final_failure(client, server):
    """
    BR_MAP_MSE_GET_MSG_LISTING_FINAL_FAILURE

    Verify that the MSE handles GetMessagesListing errors correctly when final bit is set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_fail(server, client, 'get_msg_listing', RspCode.NOT_FOUND)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_listing_non_final_failure(client, server):
    """
    BR_MAP_MSE_GET_MSG_LISTING_NON_FINAL_FAILURE

    Verify that the MSE handles GetMessagesListing errors correctly when final bit is not set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final_fail(server, client, 'get_msg_listing', RspCode.NOT_FOUND)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_listing_continue_response(client, server):
    """
    BR_MAP_MSE_GET_MSG_LISTING_CONTINUE_RESPONSE

    Verify that the MSE correctly handles Continue (0x90) response during GetMessagesListing.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_continue(server, client, 'get_msg_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_final_success(client, server):
    """
    BR_MAP_MSE_GET_MSG_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetMessage request
    with final bit set in a single response.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd(server, client, 'get_msg')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_non_final_success(client, server):
    """
    BR_MAP_MSE_GET_MSG_NON_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetMessage request
    with final bit not set (multi-packet transfer).
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final(server, client, 'get_msg')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_final_failure(client, server):
    """
    BR_MAP_MSE_GET_MSG_FINAL_FAILURE

    Verify that the MSE handles GetMessage errors correctly when final bit is set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_fail(server, client, 'get_msg', RspCode.NOT_FOUND)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_non_final_failure(client, server):
    """
    BR_MAP_MSE_GET_MSG_NON_FINAL_FAILURE

    Verify that the MSE handles GetMessage errors correctly when final bit is not set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final_fail(server, client, 'get_msg', RspCode.NOT_FOUND)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_msg_continue_response(client, server):
    """
    BR_MAP_MSE_GET_MSG_CONTINUE_RESPONSE

    Verify that the MSE correctly handles Continue (0x90) response during GetMessage.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_continue(server, client, 'get_msg')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_push_msg_success(client, server):
    """
    BR_MAP_MSE_PUSH_MSG_SUCCESS

    Verify that the MSE can successfully handle PushMessage request
    with final bit set in a single request.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_put_cmd(server, client, 'push_msg')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_push_msg_failure(client, server):
    """
    BR_MAP_MSE_PUSH_MSG_FAILURE

    Verify that the MSE handles PushMessage errors correctly when final bit is not set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_put_cmd_fail(server, client, 'push_msg', RspCode.FORBIDDEN)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_push_msg_continue_response(client, server):
    """
    BR_MAP_MSE_PUSH_MSG_CONTINUE_RESPONSE

    Verify that the MSE correctly handles Continue (0x90) response during PushMessage.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_put_cmd_continue(server, client, 'push_msg')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_convo_listing_final_success(client, server):
    """
    BR_MAP_MSE_GET_CONVO_LISTING_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetConversationListing request
    with final bit set in a single response.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd(server, client, 'get_convo_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_convo_listing_non_final_success(client, server):
    """
    BR_MAP_MSE_GET_CONVO_LISTING_NON_FINAL_SUCCESS

    Verify that the MSE can successfully handle GetConversationListing request
    with final bit not set (multi-packet transfer).
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final(server, client, 'get_convo_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_convo_listing_final_failure(client, server):
    """
    BR_MAP_MSE_GET_CONVO_LISTING_FINAL_FAILURE

    Verify that the MSE handles GetConversationListing errors correctly when final bit is set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_fail(server, client, 'get_convo_listing', RspCode.BAD_REQ)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_convo_listing_non_final_failure(client, server):
    """
    BR_MAP_MSE_GET_CONVO_LISTING_NON_FINAL_FAILURE

    Verify that the MSE handles GetConversationListing errors correctly when final bit is not set.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_final_fail(server, client, 'get_convo_listing', RspCode.BAD_REQ)
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)


def test_br_map_mse_get_convo_listing_continue_response(client, server):
    """
    BR_MAP_MSE_GET_CONVO_LISTING_CONTINUE_RESPONSE

    Verify that the MSE correctly handles Continue (0x90) response during GetConversationListing.
    """
    br_pre(server, client)
    br_connect(server, client)
    br_security(server, client)
    mas_rfcomm_connect(server, client)
    mas_connect(server, client, RspCode.SUCCESS)
    mas_get_cmd_continue(server, client, 'get_convo_listing')
    mas_disconnect(server, client, RspCode.SUCCESS)
    mas_rfcomm_disconnect(server, client)
    br_disconnect(server, client)
