"""Test fixtures and utilities for Bluetooth Classic L2CAP credit testing."""

import copy
import logging
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Type
import time

import pytest

from twister_harness import Shell, DeviceAdapter
from twister_harness.device.factory import DeviceFactory

from twister_harness.helpers.utils import find_in_config
from twister_harness.twister_harness_config import DeviceConfig
from twister_harness.helpers.domains_helper import ZEPHYR_BASE

sys.path.insert(0, os.path.join(ZEPHYR_BASE, 'scripts'))  # import zephyr_module in environment.py
sys.path.insert(0, os.path.join(ZEPHYR_BASE, 'scripts', 'pylib', 'twister'))
logger = logging.getLogger(__name__)

L2CAP_SERVER_PSM_BASIC = 0x1001
L2CAP_SERVER_PSM_RET = 0x1003
L2CAP_SERVER_PSM_FC = 0x1005
L2CAP_SERVER_PSM_ERET = 0x1007
L2CAP_SERVER_PSM_STREAM = 0x1009

MODE_OPTION = "mode_optional"


@pytest.fixture(scope='session')
def harness_devices(request, twister_harness_config):
    """Return harness_device list object."""
    from twisterlib.hardwaremap import HardwareMap

    class TwisterOptionsWrapper:
        """Wrapper class for Twister test configuration options."""

        device_flash_timeout: float = 60.0  # [s]
        device_flash_with_test: bool = True
        flash_before: bool = False

    class TwisterEnvWrapper:
        """Wrapper class for Twister test environment configuration."""

        def __init__(self, options: TwisterOptionsWrapper):
            self.options = options

    user_home = os.path.expanduser("~")
    harness_device_yml = os.path.join(user_home, request.config.getoption('--device-id') + '.yaml')
    logger.info(f'harness_device_yml:{harness_device_yml}')

    dut_build_dir = request.config.getoption('--build-dir')
    # get harness_apps from pytest args, but to keep the same command line iwth bumble test
    # harness_apps = request.config.getoption('--harness_apps')
    # logger.info(f'harness_apps:{harness_apps}')
    # harness_app_list = harness_apps.split(' ')

    # we use the default list in conftest.py for each test app
    # harness_app_list = ['samples/bluetooth/peripheral_ht']
    harness_app_list = []
    logger.info('harness_app:%s', harness_app_list)

    # reuse twister HardwareMap class to load harness device config yaml
    env = TwisterEnvWrapper(TwisterOptionsWrapper())
    harness_device_hwm = HardwareMap(env)
    harness_device_hwm.load(harness_device_yml)
    logger.info(f'harness_device_hwm[0]:{harness_device_hwm.duts[0]}')

    # reuse most dut config for harness_device, only build and flash different app into them
    dut_device_config: DeviceConfig = twister_harness_config.devices[0]
    logger.info(f'dut_device_config:{dut_device_config}')

    harness_devices = []
    for index, harness_hw in enumerate(harness_device_hwm.duts):
        if index < len(harness_app_list):
            harness_app = harness_app_list[index]
            # split harness_app into appname, extra_config
            harness_app = harness_app.split('-')
            appname, extra_config = harness_app[0], harness_app[1:]
            extra_config = ['-- -' + config for config in extra_config]
            appname = os.path.join(ZEPHYR_BASE, appname)
            # build harness_app image for harness device
            platform_in_dir = harness_hw.platform.replace('@', '_').replace('/', '_')
            build_dir = f'./harness_{platform_in_dir}_{os.path.basename(appname)}'
            cmd = (
                ['west', 'build', appname, '-b', harness_hw.platform]
                + ['--build-dir', build_dir]
                + extra_config
            )
            logger.info(' '.join(cmd))
            logger.info(os.getcwd())
            subprocess.call(' '.join(cmd), shell=True)
        else:
            build_dir = dut_build_dir

        # update the specific configuration for harness_hw
        harness_device_config = copy.deepcopy(dut_device_config)
        harness_device_config.id = harness_hw.id
        harness_device_config.serial = harness_hw.serial
        harness_device_config.build_dir = Path(build_dir)
        logger.info(f'harness_device_config:{harness_device_config}')

        # init harness device as DuT
        device_class: Type[DeviceAdapter] = DeviceFactory.get_device(harness_device_config.type)
        device_object = device_class(harness_device_config)
        device_object.initialize_log_files(request.node.name)
        harness_devices.append(device_object)

    try:
        for device_object in harness_devices:
            device_object.launch()
        yield harness_devices
    finally:  # to make sure we close all running processes execution
        for device_object in harness_devices:
            device_object.close()


class BaseBoard(object):
    """Base class for board-level test functionality."""

    def __init__(self, shell, dut):
        self.shell = shell
        self.dut = dut

    # zephyr sehll APIs
    def exec_command(self, *args, **kwargs):
        """Execute a shell command and return its output."""
        return self.shell.exec_command(*args, **kwargs)

    def readlines_until(self, *args, **kwargs):
        """Read lines from device until a condition is met, with optional timeout."""
        if 'timeout' not in kwargs:
            kwargs['timeout'] = 3
        return self.dut.readlines_until(*args, **kwargs)

    def _wait_for_shell_response(self, response, timeout=3):
        found = False
        lines = []
        try:
            for _ in range(0, timeout):
                read_lines = self.dut.readlines()
                for line in read_lines:
                    if response in line:
                        found = True
                        break
                lines = lines + read_lines
                time.sleep(1)
        except Exception as e:
            logger.error(f'{e}!', exc_info=True)
            raise e
        assert found is not False
        return found, lines

    def iexpect(self, command, response,timeout=3):
        """send command and  return output matching an expected pattern."""
        lines = self.shell.exec_command(command)
        found = any([response in line for line in lines])
        if found is False:
            found, lines = self._wait_for_shell_response(response, timeout=timeout)
        logger.info(f'{str(lines)}')
        assert found is not False
        return found, lines


class BluetoothBoard(BaseBoard):
    """Board implementation with Bluetooth Classic functionality."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.addr = None
        self.addr_type = None

    def reset(self):
        """Reset the board to its initial state."""
        self.board.reset()

    # BT test commands
    def bt_init(self, timeout=10):
        """Initialize Bluetooth on the board."""
        return self.iexpect('bt init\n', r'Bluetooth initialized[\s\S]*\n', timeout=timeout)

    def get_address(self):
        """Get the Bluetooth address of the board."""
        lines = self.exec_command('bt id-show')

        if not lines:
            return
        for line in lines:
            pattern = r"(([0-9A-Fa-f]{2}:?){6})\s\((\w+)\)"
            match = re.search(pattern, line)
            if match:
                mac_address = match.group(1)  # address
                addr_type = match.group(3)  # addr type (public/random)
                logging.info(f"MAC Address: {mac_address}")
                logging.info(f"Address Type: {addr_type}")
                logging.info(f"addr:{mac_address}, type:{addr_type}")
                self.addr, self.addr_type = mac_address, addr_type
                return (mac_address, addr_type)

    def clear(self):
        """Clear the board console's output."""
        return self.exec_command('clear\n')


# @pytest.fixture(autouse=True, scope='session')
# def testsetup(request, testenv: Testestenv):
#     testenv.set_image(testenv.programs_info['target']["filepath"], 'service')
#     testenv['download_sequence'] = ['target', 'service']
#     logging.info(f'boardunion boards:{[board.role for board in TestEnv.bu]}')
#     # bu_boards_flash
#     setup = TestSetup(testenv)
#     try:
#         setup.init_consoles()
#         setup.flash_and_run()
#     except Exception as e:
#         msg = f'fail to download images into boards, {e}'
#         pytest.exit(msg, returncode=pytest.ExitCode.TESTS_FAILED)
#     yield setup
#     setup.cleanup()


# @pytest.fixture(scope='session')
# def dut_sesson(testsetup: TestSetup):
#     duts = {}
#     for board_role in ['target', 'service']:
#         board = testsetup.bu.get_board(board_role)
#         board.reset()
#         board = BluetoothBoard(board)
#         assert board.iexpect('bt init\n', r'Bluetooth initialized[\s\S]*\n', timeout=10)
#         assert board.get_address()
#         duts[board_role] = board
#     return duts


@pytest.fixture(scope='session')
def server(shell: Shell, dut: DeviceAdapter):
    """Fixture for Bluetooth client setup."""
    shell.exec_command("bt init")
    dut.readlines_until("Bluetooth initialized", timeout=10)
    board = BluetoothBoard(shell, dut)
    board.get_address()
    lines = board.exec_command("br pscan on")
    assert any([re.search(".*connectable done.*", line) for line in lines]), (
        'fail to set connectable'
    )
    # time.sleep(.5)
    lines = board.exec_command("br iscan on")
    assert any([re.search(".*discoverable done.*", line) for line in lines]), (
        'fail to set discoverable'
    )
    lines = board.exec_command(f"l2cap_br register {str(hex(L2CAP_SERVER_PSM_BASIC))[2:]} basic")
    assert any([re.search(r"L2CAP psm[\s\S]*registered.*", line) for line in lines]), (
        f'fail to register psm {L2CAP_SERVER_PSM_BASIC}'
    )
    board.exec_command(f"l2cap_br register {str(hex(L2CAP_SERVER_PSM_RET))[2:]} ret")
    assert any([re.search(r"L2CAP psm[\s\S]*registered.*", line) for line in lines]), (
        f'fail to register psm {L2CAP_SERVER_PSM_RET}'
    )
    board.exec_command(f"l2cap_br register {str(hex(L2CAP_SERVER_PSM_FC))[2:]} fc")
    assert any([re.search(r"L2CAP psm[\s\S]*registered.*", line) for line in lines]), (
        f'fail to register psm {L2CAP_SERVER_PSM_FC}'
    )
    board.exec_command(f"l2cap_br register {str(hex(L2CAP_SERVER_PSM_ERET))[2:]} eret")
    assert any([re.search(r"L2CAP psm[\s\S]*registered.*", line) for line in lines]), (
        f'fail to register psm {L2CAP_SERVER_PSM_ERET}'
    )
    board.exec_command(f"l2cap_br register {str(hex(L2CAP_SERVER_PSM_STREAM))[2:]} stream")
    assert any([re.search(r"L2CAP psm[\s\S]*registered.*", line) for line in lines]), (
        f'fail to register psm {L2CAP_SERVER_PSM_STREAM}'
    )
    yield board


def init_shell(harness_device: DeviceAdapter):
    """Initialize a shell connection to the harness device."""
    shell = Shell(harness_device, timeout=20.0)
    config_path = Path(harness_device.device_config.app_build_dir) / 'zephyr' / '.config'
    if prompt := find_in_config(config_path, 'CONFIG_SHELL_PROMPT_UART'):
        shell.prompt = prompt
    logger.info('Wait for prompt')
    if not shell.wait_for_prompt():
        pytest.fail('Prompt not found')
    return shell


@pytest.fixture(scope='session')
def client(harness_devices):
    """Fixture for Bluetooth server setup."""
    harness_device = harness_devices[0]
    shell = init_shell(harness_device)
    board = BluetoothBoard(shell, harness_device)
    board.exec_command("bt init")
    board.readlines_until("Bluetooth initialized", timeout=10)
    yield board
    # assert board.iexpect('bt name\n', r'Local Name.*\n')
