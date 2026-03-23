# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

"""Test fixtures and utilities for Bluetooth Classic MAP testing."""

import copy
import inspect
import logging
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter, Shell, zephyr_base
from twister_harness.device.factory import DeviceFactory
from twister_harness.helpers.utils import find_in_config
from twister_harness.twister_harness_config import DeviceConfig

sys.path.insert(0, os.path.join(zephyr_base, 'scripts'))  # import zephyr_module in environment.py
sys.path.insert(0, os.path.join(zephyr_base, 'scripts', 'pylib', 'twister'))

logger = logging.getLogger(__name__)


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
    harness_device_yml = os.path.join(user_home, twister_harness_config.devices[0].id + '.yaml')
    logger.info(f'harness_device_yml:{harness_device_yml}')

    dut_build_dir = twister_harness_config.devices[0].build_dir

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
            appname = os.path.join(zephyr_base, appname)
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
        harness_device_config.serial_configs[0].serial_baud = harness_hw.serial_baud
        harness_device_config.serial_configs[0].port = harness_hw.serial
        harness_device_config.build_dir = Path(build_dir)
        logger.info(f'harness_device_config:{harness_device_config}')

        # init harness device as DuT
        device_class: type[DeviceAdapter] = DeviceFactory.get_device(harness_device_config.type)
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


class BaseBoard:
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

    def check_shell_response(self, lines, regex: list[str] | str):
        parent = inspect.stack()[1]
        module = parent.frame.f_globals['__name__']
        lineno = parent.lineno
        found = False
        dct = {}
        logger.debug('check_shell_response')

        if isinstance(regex, str):
            messages = [regex]
        else:
            messages = regex

        for message in messages:
            dct[message] = False

        for line in lines:
            logger.debug(f'{module} {lineno}: DUT response: {str(line)}')

        for message in messages:
            for line in lines:
                if re.search(message, line):
                    dct[message] = True
                    for key in dct:
                        if dct[key] is False:
                            found = False
                            break
                    else:
                        found = True
                    break

        for key in dct:
            logger.debug(f'{module} {lineno}: Expected DUT response: "{key}", Matched: {dct[key]}')

        return found

    def wait_for_shell_response(self, regex: list[str] | str, timeout=10):
        parent = inspect.stack()[1]
        module = parent.frame.f_globals['__name__']
        lineno = parent.lineno
        found = False
        lines = []
        dct = {}

        logger.debug('wait_for_shell_response')

        if isinstance(regex, str):
            messages = [regex]
        else:
            messages = regex

        for message in messages:
            dct[message] = False

        try:
            for _ in range(0, timeout):
                read_lines = self.dut.readlines(print_output=False)
                for line in read_lines:
                    logger.debug(f'{module} {lineno}: DUT response: {str(line)}')
                lines += read_lines
                for message in messages:
                    for line in read_lines:
                        if re.search(message, line):
                            dct[message] = True
                            for key in dct:
                                if dct[key] is False:
                                    found = False
                                    break
                            else:
                                found = True
                            break
                if found is True:
                    break
                time.sleep(1)
        except Exception as e:
            logger.error(f'{e}!', exc_info=True)
            raise e

        for key in dct:
            logger.debug(f'{module} {lineno}: Expected DUT response: "{key}", Matched: {dct[key]}')

        return found, lines

    def iexpect(self, command, response: list[str] | str, wait=True, timeout=10):
        """send command and  return output matching an expected pattern."""
        lines = self.shell.exec_command(command)
        if wait:
            found, lines = self.wait_for_shell_response(response, timeout=timeout)
        else:
            found = self.check_shell_response(lines, response)

        assert found is not False
        return found, lines


class BluetoothBoard(BaseBoard):
    """Board implementation with Bluetooth Classic functionality."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.pub_addr = None
        self.rnd_addr = None
        self.addr_type = None
        self.channel = None
        self.psm = None
        self.instance_id = None
        self.supported_features = None

    def reset(self):
        """Reset the board to its initial state."""
        self.board.reset()

    # BT test commands
    def bt_init(self, timeout=10):
        """Initialize Bluetooth on the board."""
        return self.iexpect('bt init\n', r'Bluetooth initialized[\s\S]*\n', timeout=timeout)

    def bt_connectable(self):
        """Set Bluetooth device connectable."""
        lines = self.exec_command("br pscan on")
        assert any([re.search(".*connectable done.*", line) for line in lines]), (
            'fail to set connectable'
        )

    def bt_discoverable(self):
        """Set Bluetooth device discoverable."""
        lines = self.exec_command("br iscan on")
        assert any([re.search(".*discoverable done.*", line) for line in lines]), (
            'fail to set discoverable'
        )

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
                if addr_type == 'public':
                    self.pub_addr = mac_address
                if addr_type == 'random':
                    self.rnd_addr = mac_address
                self.addr_type = addr_type
                return (mac_address, addr_type)

    def clear(self):
        """Clear the board console's output."""
        return self.exec_command('clear\n')

    def map_init(self):
        """map initialization."""
        self.exec_command('bt auth none')

        # Register L2CAP server
        lines = self.exec_command('test_map mse mas l2cap_register')
        pattern = (
            r"L2CAP server \(psm (?P<psm>\w+), mas_instance_id (?P<instance_id>\w+), "
            r"map_supported_features (?P<supported_features>\w+)\) is registered"
        )
        found = self.check_shell_response(lines, pattern)
        assert found is True

        for line in lines:
            match = re.search(pattern, line)
            if match:
                psm = match.group('psm')
                instance_id = match.group('instance_id')
                supported_features = match.group('supported_features')
                break

        # Register RFCOMM server
        lines = self.exec_command('test_map mse mas rfcomm_register')
        pattern = (
            r"RFCOMM server \(channel (?P<channel>\w+), mas_instance_id (?P<instance_id>\w+), "
            r"map_supported_features (?P<supported_features>\w+)\) is registered"
        )
        found = self.check_shell_response(lines, pattern)
        assert found is True

        for line in lines:
            match = re.search(pattern, line)
            if match:
                channel = match.group('channel')
                instance_id = match.group('instance_id')
                supported_features = match.group('supported_features')
                break

        self.channel = int(channel, 16)
        self.psm = int(psm, 16)
        self.instance_id = int(instance_id, 10)
        self.supported_features = int(supported_features, 16)


@pytest.fixture(scope='session')
def client(shell: Shell, dut: DeviceAdapter):
    """Fixture for Bluetooth client setup."""
    logger.info('Fixture for Bluetooth client setup.')
    shell.exec_command("bt init")
    dut.readlines_until(regex="Bluetooth initialized", timeout=10)
    board = BluetoothBoard(shell, dut)
    board.get_address()
    board.bt_connectable()
    board.bt_discoverable()
    board.map_init()
    yield board


def init_shell(harness_device: DeviceAdapter):
    """Initialize a shell connection to the harness device."""
    logger.info('Initialize a shell connection to the harness device.')
    shell = Shell(harness_device, timeout=20.0)
    config_path = Path(harness_device.device_config.app_build_dir) / 'zephyr' / '.config'
    if prompt := find_in_config(config_path, 'CONFIG_SHELL_PROMPT_UART'):
        shell.prompt = prompt
    logger.info('Wait for prompt')
    if not shell.wait_for_prompt():
        pytest.fail('Prompt not found')
    return shell


@pytest.fixture(scope='session')
def server(harness_devices):
    """Fixture for Bluetooth server setup."""
    logger.info('Fixture for Bluetooth server setup.')
    harness_device = harness_devices[0]
    shell = init_shell(harness_device)
    board = BluetoothBoard(shell, harness_device)
    board.exec_command("bt init")
    board.readlines_until(regex="Bluetooth initialized", timeout=10)
    board.get_address()
    board.bt_connectable()
    board.bt_discoverable()
    board.map_init()
    yield board
