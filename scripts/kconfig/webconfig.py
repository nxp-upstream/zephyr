#!/usr/bin/env python3

# Copyright (c) 2024, Web Kconfig Implementation
# SPDX-License-Identifier: ISC

"""
Web-based Kconfig configuration interface using Flask.
Provides a modern web UI for configuring Kconfig options.
"""

import os
import sys
import json
import argparse
import signal
import threading
import time
from pathlib import Path
from werkzeug.serving import make_server

from flask import Flask, render_template, request, jsonify, send_from_directory
from flask_cors import CORS

from kconfiglib import (
    Kconfig, Symbol, Choice, MENU, COMMENT,
    BOOL, TRISTATE, STRING, INT, HEX,
    expr_value, TRI_TO_STR, TYPE_TO_STR,
    standard_kconfig, standard_config_filename
)

app = Flask(__name__,
    template_folder='webconfig_ui/templates',
    static_folder='webconfig_ui/static'
)
CORS(app)

# Global Kconfig instance
kconf = None
config_filename = None
config_changed = False
server = None
last_heartbeat = time.time()
shutdown_thread = None

def init_kconfig(kconfig_file=None):
    """Initialize Kconfig instance"""
    global kconf, config_filename

    if kconfig_file:
        kconf = Kconfig(kconfig_file)
    else:
        kconf = standard_kconfig(__doc__)

    config_filename = standard_config_filename()

    # Load existing configuration
    if os.path.exists(config_filename):
        print(kconf.load_config(config_filename))

    return kconf

def safe_expr_str(expr):
    """
    Safe version of expr_str that avoids deep recursion.
    Returns a simplified string representation of the expression.
    """
    try:
        # Try to get string representation with limited depth
        return _limited_expr_str(expr, max_depth=10)
    except:
        # If that fails, return a simple placeholder
        return "<complex expression>"

def _limited_expr_str(expr, max_depth=10, current_depth=0):
    """
    Limited depth expression string conversion to avoid recursion errors.
    """
    if current_depth >= max_depth:
        return "..."

    if not isinstance(expr, tuple):
        # It's a symbol or constant
        if hasattr(expr, 'name'):
            return expr.name
        return str(expr)

    # Handle operators
    if len(expr) == 2:  # NOT
        return "!{}".format(_limited_expr_str(expr[1], max_depth, current_depth + 1))
    elif len(expr) == 3:  # Binary operator
        op = expr[0]
        left = _limited_expr_str(expr[1], max_depth, current_depth + 1)
        right = _limited_expr_str(expr[2], max_depth, current_depth + 1)

        if op == 1:  # AND (assuming these are the token values)
            return "{} && {}".format(left, right)
        elif op == 2:  # OR
            return "{} || {}".format(left, right)
        else:
            return "{} ? {}".format(left, right)

    return str(expr)

def node_to_dict(node):
    """Convert a Kconfig node to dictionary for JSON serialization"""
    item = node.item

    result = {
        'id': str(id(node)),
        'type': 'unknown',
        'name': '',
        'prompt': '',
        'help': '',
        'visible': False,
        'children': []
    }

    # Get prompt text
    if hasattr(node, 'prompt') and node.prompt:
        result['prompt'] = node.prompt[0]
        # Safe visibility check
        try:
            result['visible'] = expr_value(node.prompt[1]) > 0
        except:
            result['visible'] = False

    # Get help text - only for Symbol and Choice nodes
    if hasattr(node, 'help') and node.help is not None:
        result['help'] = node.help

    # Handle different item types
    if item == MENU:
        result['type'] = 'menu'
        result['name'] = result['prompt'] if result['prompt'] else 'Menu'
    elif item == COMMENT:
        result['type'] = 'comment'
        result['name'] = result['prompt'] if result['prompt'] else ''
    elif isinstance(item, Symbol):
        result['type'] = 'symbol'
        result['name'] = item.name
        result['symbol_type'] = TYPE_TO_STR.get(item.orig_type, 'unknown')

        try:
            result['value'] = item.str_value
        except:
            result['value'] = ''

        result['user_value'] = item.user_value

        if item.orig_type in (BOOL, TRISTATE):
            try:
                result['tri_value'] = item.tri_value
                result['assignable'] = list(item.assignable)
            except:
                result['tri_value'] = 0
                result['assignable'] = []
        elif item.orig_type in (STRING, INT, HEX):
            result['ranges'] = []
            if hasattr(item, 'ranges') and item.ranges:
                for low, high, cond, _ in item.ranges:
                    try:
                        if expr_value(cond):
                            result['ranges'].append({
                                'low': low.str_value if hasattr(low, 'str_value') else str(low),
                                'high': high.str_value if hasattr(high, 'str_value') else str(high)
                            })
                    except:
                        pass

        # Add dependency info with safe string conversion
        try:
            if hasattr(item, 'direct_dep') and item.direct_dep:
                result['depends_on'] = safe_expr_str(item.direct_dep)
            else:
                result['depends_on'] = None
        except:
            result['depends_on'] = None

        result['selected_by'] = []
        result['implied_by'] = []

        # Check if symbol is new (no user value)
        try:
            result['is_new'] = item.user_value is None and item.type and not item.choice
        except:
            result['is_new'] = False

    elif isinstance(item, Choice):
        result['type'] = 'choice'
        result['name'] = item.name if item.name else '<choice>'
        try:
            result['mode'] = item.str_value
            result['selection'] = item.selection.name if item.selection else None
            result['symbols'] = [sym.name for sym in item.syms]
        except:
            result['mode'] = 'n'
            result['selection'] = None
            result['symbols'] = []

    return result

def build_menu_tree(menu_node, show_all=False, max_depth=20, current_depth=0):
    """Build menu tree structure with depth limit to avoid stack overflow"""
    if current_depth >= max_depth:
        return []

    items = []
    node = menu_node.list if hasattr(menu_node, 'list') else None

    while node:
        try:
            item_dict = node_to_dict(node)

            # Check visibility
            if show_all or item_dict['visible'] or item_dict['type'] in ('menu', 'comment'):
                # Process children for menus and choices
                if hasattr(node, 'list') and node.list:
                    # Don't recurse into symbol nodes (they shouldn't have meaningful children in the UI)
                    if not isinstance(node.item, Symbol) or node.is_menuconfig:
                        child_items = build_menu_tree(
                            node, show_all, max_depth, current_depth + 1
                        )
                        if child_items:
                            item_dict['children'] = child_items

                items.append(item_dict)
        except Exception as e:
            # Log the error but continue processing
            print(f"Warning: Error processing node: {e}")

        node = node.next if hasattr(node, 'next') else None
    return items
# 在全局变量部分添加
session_changes = {}  # 记录本次会话的改动

@app.route('/')
def index():
    """Main page"""
    return render_template('index.html')

@app.route('/api/set_value', methods=['POST'])
def set_value():
    """Set symbol value"""
    global config_changed, session_changes

    data = request.json
    symbol_name = data.get('symbol')
    value = data.get('value')

    sym = kconf.syms.get(symbol_name)
    if not sym:
        return jsonify({'error': 'Symbol not found'}), 404

    try:
        if symbol_name not in session_changes:
            session_changes[symbol_name] = {
                'original': sym.str_value,
                'original_user': sym.user_value
            }

        # Convert value based on symbol type
        if sym.orig_type in (BOOL, TRISTATE):
            if value in ('n', 'm', 'y'):
                sym.set_value(value)
            elif value in ('0', '1', '2'):
                sym.set_value(TRI_TO_STR[int(value)])
            else:
                sym.set_value(value)
        else:
            sym.set_value(str(value))

        # 更新会话改动记录
        session_changes[symbol_name]['current'] = sym.str_value

        # 如果改回原始值，从改动列表中移除
        if sym.str_value == session_changes[symbol_name]['original']:
            del session_changes[symbol_name]

        config_changed = True

        return jsonify({
            'success': True,
            'new_value': sym.str_value,
            'changed': config_changed,
            'session_changes': len(session_changes)
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 400

# 修改 get_changes 路由
@app.route('/api/changes')
def get_changes():
    """Get configuration changes - either session only or all"""
    mode = request.args.get('mode', 'session')  # 'session' or 'all'
    changes = []

    if mode == 'session':
        # 只显示本次会话的改动
        for symbol_name, change_info in session_changes.items():
            sym = kconf.syms.get(symbol_name)
            if not sym:
                continue

            change = {
                'name': sym.name,
                'type': TYPE_TO_STR.get(sym.orig_type, 'unknown'),
                'original': change_info['original'],
                'value': sym.str_value,
                'prompt': '',
            }

            # Get prompt for display
            for node in sym.nodes:
                if hasattr(node, 'prompt') and node.prompt:
                    change['prompt'] = node.prompt[0]
                    break

            changes.append(change)

    else:  # mode == 'all'
        # 显示所有与默认值不同的配置
        for sym in kconf.unique_defined_syms:
            if sym.user_value is not None:
                change = {
                    'name': sym.name,
                    'type': TYPE_TO_STR.get(sym.orig_type, 'unknown'),
                    'value': sym.str_value,
                    'user_value': sym.user_value,
                    'default': None,
                    'prompt': '',
                    'is_new': False
                }

                # Get default value
                try:
                    for default, cond in sym.defaults:
                        if expr_value(cond):
                            if isinstance(default, Symbol):
                                change['default'] = default.str_value
                            else:
                                change['default'] = str(default)
                            break
                except:
                    pass

                # Check if it's different from default
                if change['default'] is None:
                    if sym.orig_type in (BOOL, TRISTATE):
                        if sym.str_value != 'n':
                            change['is_new'] = True
                            changes.append(change)
                    else:
                        change['is_new'] = True
                        changes.append(change)
                elif change['value'] != change['default']:
                    changes.append(change)

                # Get prompt for display
                for node in sym.nodes:
                    if hasattr(node, 'prompt') and node.prompt:
                        change['prompt'] = node.prompt[0]
                        break

    # Sort by name
    changes.sort(key=lambda x: x['name'])

    return jsonify({
        'changes': changes,
        'count': len(changes),
        'mode': mode
    })

# 修改 reset_symbol 路由
@app.route('/api/reset_symbol', methods=['POST'])
def reset_symbol():
    """Reset a symbol to its default value or session start value"""
    global config_changed, session_changes

    symbol_name = request.json.get('symbol')
    reset_type = request.json.get('type', 'session')  # 'session' or 'default'

    sym = kconf.syms.get(symbol_name)
    if not sym:
        return jsonify({'error': 'Symbol not found'}), 404

    try:
        if reset_type == 'session' and symbol_name in session_changes:
            # 恢复到会话开始时的值
            original_value = session_changes[symbol_name]['original']
            if sym.orig_type in (BOOL, TRISTATE):
                sym.set_value(original_value)
            else:
                sym.set_value(str(original_value))

            # 从改动列表中移除
            del session_changes[symbol_name]
        else:
            # 恢复到默认值
            sym.unset_value()
            # 如果在会话改动中，更新记录
            if symbol_name in session_changes:
                if sym.str_value == session_changes[symbol_name]['original']:
                    del session_changes[symbol_name]

        config_changed = True

        return jsonify({
            'success': True,
            'new_value': sym.str_value,
            'changed': config_changed,
            'session_changes': len(session_changes)
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 400

# 修改 save_config 路由
@app.route('/api/save_config', methods=['POST'])
def save_config():
    """Save configuration to file"""
    global config_changed, session_changes

    try:
        filename = request.json.get('filename', config_filename)
        msg = kconf.write_config(filename)
        config_changed = False
        session_changes.clear()  # 清空会话改动记录
        return jsonify({
            'success': True,
            'message': msg,
            'filename': filename
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500
@app.route('/api/save_and_exit', methods=['POST'])
def save_and_exit():
    """Save configuration and shutdown server"""
    global config_changed, server

    try:
        # Save configuration
        filename = request.json.get('filename', config_filename)
        msg = kconf.write_config(filename)
        config_changed = False

        # Schedule shutdown
        def shutdown():
            time.sleep(0.5)  # Give time for response to be sent
            if server:
                server.shutdown()

        threading.Thread(target=shutdown).start()

        return jsonify({
            'success': True,
            'message': msg,
            'filename': filename
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/heartbeat', methods=['POST'])
def heartbeat():
    """Keep-alive endpoint to detect when browser closes"""
    global last_heartbeat
    last_heartbeat = time.time()
    return jsonify({'status': 'ok'})

def monitor_heartbeat():
    """Monitor heartbeat and shutdown if no heartbeat for 10 seconds"""
    global server
    while True:
        time.sleep(5)
        if time.time() - last_heartbeat > 10:
            print("\nNo heartbeat detected. Browser closed. Shutting down...")
            if server:
                server.shutdown()
            break
@app.route('/api/menu_tree')
def get_menu_tree():
    """Get the complete menu tree"""
    show_all = request.args.get('show_all', 'false').lower() == 'true'
    try:
        tree = build_menu_tree(kconf.top_node, show_all)
        return jsonify(tree)
    except Exception as e:
        print(f"Error building menu tree: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/symbol/<symbol_name>')
def get_symbol_info(symbol_name):
    """Get detailed information about a symbol"""
    sym = kconf.syms.get(symbol_name)
    if not sym:
        return jsonify({'error': 'Symbol not found'}), 404

    info = {
        'name': sym.name,
        'type': TYPE_TO_STR.get(sym.orig_type, 'unknown'),
        'value': '',
        'user_value': sym.user_value,
        'help': '',
        'depends_on': None,
        'defaults': [],
        'selects': [],
        'implies': [],
        'assignable': [],
        'prompt': ''
    }

    # Get value safely
    try:
        info['value'] = sym.str_value
    except:
        info['value'] = ''

    # Get assignable values for bool/tristate
    if sym.orig_type in (BOOL, TRISTATE):
        try:
            info['assignable'] = list(sym.assignable)
        except:
            info['assignable'] = []

    # Get prompt and help text from nodes
    for node in sym.nodes:
        if hasattr(node, 'prompt') and node.prompt and not info['prompt']:
            info['prompt'] = node.prompt[0]
        if hasattr(node, 'help') and node.help and not info['help']:
            info['help'] = node.help

    # Get dependencies with safe conversion
    try:
        if hasattr(sym, 'direct_dep') and sym.direct_dep:
            info['depends_on'] = safe_expr_str(sym.direct_dep)
    except:
        pass

    # Get defaults
    try:
        if hasattr(sym, 'orig_defaults'):
            for val, cond in sym.orig_defaults:
                default_info = {
                    'value': safe_expr_str(val) if isinstance(val, tuple) else str(val),
                    'condition': safe_expr_str(cond) if cond else None
                }
                info['defaults'].append(default_info)
    except:
        pass

    # Get selects
    try:
        if hasattr(sym, 'orig_selects'):
            for target, cond in sym.orig_selects:
                select_info = {
                    'target': target.name if hasattr(target, 'name') else str(target),
                    'condition': safe_expr_str(cond) if cond else None
                }
                info['selects'].append(select_info)
    except:
        pass

    # Get implies
    try:
        if hasattr(sym, 'orig_implies'):
            for target, cond in sym.orig_implies:
                imply_info = {
                    'target': target.name if hasattr(target, 'name') else str(target),
                    'condition': safe_expr_str(cond) if cond else None
                }
                info['implies'].append(imply_info)
    except:
        pass

    return jsonify(info)

@app.route('/api/load_config', methods=['POST'])
def load_config():
    """Load configuration from file"""
    global config_changed

    try:
        filename = request.json.get('filename', config_filename)
        msg = kconf.load_config(filename)
        config_changed = False
        return jsonify({
            'success': True,
            'message': msg,
            'filename': filename
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/search')
def search():
    """Search for symbols"""
    query = request.args.get('q', '').lower()
    if not query or len(query) < 2:
        return jsonify([])

    results = []
    count = 0

    for sym in kconf.unique_defined_syms:
        if count >= 50:  # Limit results
            break

        if query in sym.name.lower():
            try:
                result = {
                    'name': sym.name,
                    'type': TYPE_TO_STR.get(sym.orig_type, 'unknown'),
                    'value': sym.str_value,
                    'prompt': '',
                    'visible': False
                }

                # Get prompt and visibility if available
                for node in sym.nodes:
                    if hasattr(node, 'prompt') and node.prompt:
                        result['prompt'] = node.prompt[0]
                        try:
                            result['visible'] = expr_value(node.prompt[1]) > 0
                        except:
                            result['visible'] = False
                        break

                results.append(result)
                count += 1
            except Exception as e:
                # Skip problematic symbols
                print(f"Warning: Skipping symbol {sym.name}: {e}")
                continue

    return jsonify(results)

@app.route('/api/stats')
def get_stats():
    """Get configuration statistics"""
    try:
        total_symbols = len(kconf.unique_defined_syms)
        set_symbols = sum(1 for sym in kconf.unique_defined_syms if sym.user_value is not None)

        return jsonify({
            'total_symbols': total_symbols,
            'set_symbols': set_symbols,
            'config_file': config_filename,
            'changed': config_changed
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500

def main():
    """Main entry point"""
    global server, shutdown_thread, last_heartbeat

    parser = argparse.ArgumentParser(description='Web-based Kconfig configuration')
    parser.add_argument('kconfig', nargs='?', default='Kconfig',
                        help='Top-level Kconfig file (default: Kconfig)')
    parser.add_argument('--port', type=int, default=5000,
                        help='Port to run web server on (default: 5000)')
    parser.add_argument('--host', default='127.0.0.1',
                        help='Host to bind to (default: 127.0.0.1)')
    parser.add_argument('--no-auto-close', action='store_true',
                        help='Disable auto-close when browser closes')
    parser.add_argument('--debug', action='store_true',
                        help='Run in debug mode')

    args = parser.parse_args()

    sys.setrecursionlimit(3000)

    try:
        init_kconfig(args.kconfig)
    except Exception as e:
        print(f"Error loading Kconfig: {e}")
        sys.exit(1)

    print(f"Starting Kconfig Web Interface on http://{args.host}:{args.port}")
    print(f"Configuration file: {config_filename}")
    print("Press Ctrl+C to exit")

    # Start heartbeat monitor if auto-close is enabled
    if not args.no_auto_close and not args.debug:
        last_heartbeat = time.time()
        shutdown_thread = threading.Thread(target=monitor_heartbeat, daemon=True)
        shutdown_thread.start()

    # Create server
    server = make_server(args.host, args.port, app)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        if server:
            server.shutdown()

if __name__ == '__main__':
    main()
