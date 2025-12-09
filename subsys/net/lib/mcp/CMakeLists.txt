# Copyright 2025 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_common.c)
zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_server.c)
zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_transport.c)
zephyr_library_sources_ifdef(CONFIG_MCP_SERVER mcp_json.c)
zephyr_library_sources_ifdef(CONFIG_MCP_TRANSPORT_HTTP mcp_transport_http.c)
zephyr_linker_sources_ifdef(CONFIG_MCP_TRANSPORT_HTTP SECTIONS mcp_http.ld)