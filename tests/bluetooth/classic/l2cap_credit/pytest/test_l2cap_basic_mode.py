"""Test cases for Bluetooth Classic L2CAP basic mode functionality."""

import logging
import re

def test_l2cap_basic_mode_tc01(client, server):
    """Test L2CAP basic mode functionality."""

    lines = server.exec_command('br l2cap register 1001 none')
    pattern = r'L2CAP psm[\s\S]*registered'
    assert any([re.search(pattern, line) for line in lines]), \
        'fail to register L2CAP psm'

    # assert server.readlines_until(r'L2CAP psm[\s\S]*registered.*\n')
    lines = server.exec_command('br pscan on')
    assert any([re.search(r'connectable done', line) for line in lines]), 'fail to start pscan'

    # assert server.readlines_until(r'connectable done.*\n')
    lines = server.exec_command('br iscan on')
    assert any([re.search(r'discoverable done', line) for line in lines]), 'fail to start iscan'


    logging.info(f'will connect {server.addr}')
    # assert client.iexpect('br discovery on\n', r'Discovery Started.*\n')
    # assert client.readlines_until(fr'\[DEVICE\].*?{server.addr}[\s\S]*\n', timeout=20)


    # assert client.iexpect(f'br connect {server.addr}\n', r'Connected.*\n', timeout=10)
    # time.sleep(.5)
    # assert client.iexpect('br l2cap connect 1001 none\n', r'Channel[\s\S]*connected.*\n')

    # assert client.iexpect('br l2cap disconnect\n', r'Channel[\s\S]*disconnected.*\n')
    # assert client.iexpect('bt disconnect\n', r'Disconnected.*\n')
