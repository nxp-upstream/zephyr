# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for Kconfig devicetree and shield selection functions."""

from pathlib import Path
from types import SimpleNamespace

import kconfigfunctions
import kconfiglib
import pytest


class _Kconf(SimpleNamespace):
    filename = "Kconfig.shield"
    linenr = 5


def test_shields_list_contains_exact_match(monkeypatch):
    monkeypatch.setenv("SHIELD_AS_LIST", "myshield;other")
    assert kconfigfunctions.shields_list_contains(_Kconf(), None, "myshield") == "y"
    assert kconfigfunctions.shields_list_contains(_Kconf(), None, "missing") == "n"


def test_shields_list_contains_missing_env(monkeypatch):
    monkeypatch.delenv("SHIELD_AS_LIST", raising=False)
    assert kconfigfunctions.shields_list_contains(_Kconf(), None, "myshield") == "n"


def test_shields_list_contains_strips_leading_space(monkeypatch, capsys):
    monkeypatch.setenv("SHIELD_AS_LIST", "myshield")
    assert kconfigfunctions.shields_list_contains(_Kconf(), None, " myshield") == "y"
    err = capsys.readouterr().out
    assert 'searching for shield " myshield", did you mean "myshield" (without a space)' in err


def test_shields_list_contains_strips_trailing_space(monkeypatch, capsys):
    monkeypatch.setenv("SHIELD_AS_LIST", "myshield")
    assert kconfigfunctions.shields_list_contains(_Kconf(), None, "myshield ") == "y"
    err = capsys.readouterr().out
    assert 'did you mean "myshield"' in err


def test_shields_list_contains_internal_whitespace(monkeypatch, capsys):
    monkeypatch.setenv("SHIELD_AS_LIST", "my shield")
    assert kconfigfunctions.shields_list_contains(_Kconf(), None, "my shield") == "n"
    err = capsys.readouterr().out
    assert 'contains whitespace' in err


@pytest.mark.parametrize(
    "systick_compat",
    [
        "arm,armv6m-systick",
        "arm,armv7m-systick",
        "arm,armv8m-systick",
        "arm,armv8.1m-systick",
    ],
)
@pytest.mark.parametrize(
    "chosen, status, expected",
    [
        (None, "okay", ("y", "y")),
        ("systick", "okay", ("y", "n")),
        ("ostimer", "okay", ("n", "y")),
        ("other", "okay", ("n", "n")),
        ("systick", "disabled", ("n", "n")),
        ("ostimer", "disabled", ("n", "n")),
    ],
)
def test_system_timer_chosen(monkeypatch, tmp_path, systick_compat, chosen, status, expected):
    nodes = {
        "systick": SimpleNamespace(path="/systick", status="okay", compats=[systick_compat]),
        "ostimer": SimpleNamespace(path="/ostimer", status="okay", compats=["nxp,os-timer"]),
        "other": SimpleNamespace(path="/other", status="okay", compats=["nxp,lptmr"]),
    }
    if chosen is not None:
        nodes[chosen].status = status
    monkeypatch.setattr(kconfigfunctions, "doc_mode", False)
    monkeypatch.setattr(
        kconfigfunctions,
        "edt",
        SimpleNamespace(
            chosen_node=lambda name: nodes.get(chosen) if name == "zephyr,system-timer" else None,
            label2node={"systick": nodes["systick"]},
        ),
    )

    root = Path(__file__).resolve().parents[2]
    compat_symbol = systick_compat.upper().replace(",", "_").replace("-", "_").replace(".", "_")
    enabled = [
        "CPU_CORTEX_M_HAS_SYSTICK",
        "DT_HAS_NXP_OS_TIMER_ENABLED",
        f"DT_HAS_{compat_symbol}_ENABLED",
    ]
    disabled = [
        "TICKLESS_CAPABLE",
        "TIMER_HAS_64BIT_CYCLE_COUNTER",
        "RESET",
        "SYSTEM_TIMER_HAS_LPM_COMPANION_SUPPORT",
        "SYSTEM_TIMER_HAS_DISABLE_SUPPORT",
        "SYSTEM_TIMER_RESET_BY_LPM",
        "DEPRECATED",
    ]
    text = "DT_COMPAT_NXP_OS_TIMER := nxp,os-timer\n"
    text += "".join(f"config {name}\n\tdef_bool y\n\n" for name in enabled)
    text += "".join(f"config {name}\n\tbool\n\n" for name in disabled)
    text += "config SYS_CLOCK_HW_CYCLES_PER_SEC\n\tint\n\tdefault 1000000\n\n"
    for filename in ("Kconfig.cortex_m_systick", "Kconfig.mcux_os"):
        text += f'source "{(root / "drivers/timer" / filename).as_posix()}"\n'
    config_path = tmp_path / "Kconfig"
    config_path.write_text(text, encoding="utf-8")
    config = kconfiglib.Kconfig(str(config_path), warn=False)
    timers = [config.syms["CORTEX_M_SYSTICK"], config.syms["MCUX_OS_TIMER"]]
    assert tuple(timer.str_value for timer in timers) == expected
    for timer in timers:
        timer.set_value("y")
    assert tuple(timer.str_value for timer in timers) == expected
