# Copyright (c) 2025 NXP
# SPDX-License-Identifier: Apache-2.0

import logging

logger = logging.getLogger(__name__)

# def test_l2cap_credit_mode_TC01(client, server):

#     logging.info(f'cached server addr:{server.addr}')
#     addr = server.get_address()
#     assert addr
#     logging.info(f'new server addr:{addr[0]}, {addr[1]}')

#     assert server.iexpect('bt name\n', r'Local Name.*\n')


# def test_l2cap_credit_mode_TC02(client, server):

#     assert server.iexpect('bt name\n', r'Local Name.*\n')


def test_l2cap_credit_mode_TC03(client, server):

    logger.info('register L2CAP psm....................................')

    # lines = client.exec_command('br l2cap register 1001 none')
    # pattern = r'L2CAP psm[\s\S]*registered'
    # assert any([re.search(pattern, line) for line in lines]), \
    #     'fail to register L2CAP psm'
