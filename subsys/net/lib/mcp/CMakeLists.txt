# Copyright 2025 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_common.c)
zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_server.c)
zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_transport.c)
zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_json.c)
