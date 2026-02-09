.. _mcp_server:

MCP Server
##########

.. contents::
    :local:
    :depth: 2

Overview
********

MCP (Model Context Protocol) is an open-source standard for connecting
AI applications to external systems. The MCP specification is described
at https://modelcontextprotocol.io.

This Zephyr MCP Server library aims to bring the server functionality to MCUs,
allowing them to expose tools and resources to AI agents. The primary focus
in the initial release is on Tools and building the foundation for future improvements.

Key features of this release:

- MCP Specification version 2025-11-25 compliance
- HTTP transport protocol (in a transport-agnostic architecture)
- Tool lifecycle management
- Request cancellation and timeout monitoring
- Health monitoring for long-running executions

Architecture
************

The MCP server is structured into several layers:

.. code-block:: none

    ┌─────────────────────────────────────┐
    │      Application Layer              │
    │  (Tool Registration & Callbacks)    │
    └──────────────┬──────────────────────┘
                   │
    ┌──────────────▼──────────────────────┐
    │        MCP Core Server              │
    │  - Request routing                  │
    │  - Client lifecycle management      │
    │  - Execution tracking               │
    └──────────────┬──────────────────────┘
                   │
    ┌──────────────▼──────────────────────┐
    │         Transport Layer             │
    │       (HTTP, can be extended)       │
    └─────────────────────────────────────┘

Thread Model
============

The MCP server uses a multi-threaded architecture:

- **Main application thread**: Tool registration, initialization
- **Transport threads**: Handle incoming requests (transport-specific)
- **Worker threads**: Process MCP requests asynchronously (configurable via
  :kconfig:option:`CONFIG_MCP_REQUEST_WORKERS`)
- **Health monitor thread**: Monitors execution timeouts

Client Lifecycle
================

Clients are stored in a fixed-size pool managed by the MCP server. They progress
through the following states:

1. **DEINITIALIZED**: No client allocated
2. **NEW**: Client allocated, not initialized
3. **INITIALIZING**: Initialize response sent
4. **INITIALIZED**: Client confirmed initialization (ready for requests)

Tool Execution Flow
===================

1. Client sends ``tools/call`` request
2. Server validates client state and tool existence
3. Creates execution context with unique token
4. Invokes tool callback in worker thread context
5. Tool processes request (may be async)
6. Tool submits response via :c:func:`mcp_server_submit_tool_message`
7. Server sends response to client and cleans up execution context

Tool executions are tracked internally and monitored for timeouts. They can be
cancelled at any time based on an internal timeout or a request from the client
that initiated the execution. The cancellation is sent to the application layer
via an event in the tool callback.

Configuration Options
*********************

Enable MCP server support:

.. code-block:: kconfig

   CONFIG_MCP_SERVER=y

Common configuration options:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Option
     - Default
     - Description
   * - :kconfig:option:`CONFIG_MCP_SERVER_COUNT`
     - 1
     - Maximum number of concurrent MCP server instances
   * - :kconfig:option:`CONFIG_MCP_MAX_CLIENTS`
     - 4
     - Maximum clients per server instance
   * - :kconfig:option:`CONFIG_MCP_MAX_TOOLS`
     - 16
     - Maximum tools that can be registered
   * - :kconfig:option:`CONFIG_MCP_REQUEST_WORKERS`
     - 2
     - Number of worker threads for request processing
   * - :kconfig:option:`CONFIG_MCP_REQUEST_WORKER_STACK_SIZE`
     - 4096
     - Stack size for each worker thread (increase if tools need more stack)
   * - :kconfig:option:`CONFIG_MCP_MAX_CLIENT_REQUESTS`
     - 4
     - Maximum concurrent requests per client

Timeout Configuration
=====================

.. list-table::
   :header-rows: 1
   :widths: 35 15 50

   * - Option
     - Default
     - Description
   * - :kconfig:option:`CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS`
     - 60000
     - Maximum tool execution time (ms)
   * - :kconfig:option:`CONFIG_MCP_TOOL_CANCEL_TIMEOUT_MS`
     - 5000
     - Timeout for tool to acknowledge cancellation (ms)
   * - :kconfig:option:`CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS`
     - 30000
     - Maximum idle time for long-running executions (ms)
   * - :kconfig:option:`CONFIG_MCP_CLIENT_TIMEOUT_MS`
     - 300000
     - Client idle timeout (ms)
   * - :kconfig:option:`CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS`
     - 1000
     - Health monitor check interval (ms)

Tool Configuration
==================

.. list-table::
   :header-rows: 1
   :widths: 35 15 50

   * - Option
     - Default
     - Description
   * - :kconfig:option:`CONFIG_MCP_TOOL_NAME_MAX_LEN`
     - 64
     - Maximum tool name length
   * - :kconfig:option:`CONFIG_MCP_TOOL_SCHEMA_MAX_LEN`
     - 512
     - Maximum JSON schema length
   * - :kconfig:option:`CONFIG_MCP_TOOL_DESC`
     - y
     - Include tool descriptions
   * - :kconfig:option:`CONFIG_MCP_TOOL_DESC_MAX_LEN`
     - 256
     - Maximum description length
   * - :kconfig:option:`CONFIG_MCP_TOOL_TITLE`
     - n
     - Include tool titles
   * - :kconfig:option:`CONFIG_MCP_TOOL_OUTPUT_SCHEMA`
     - n
     - Include output schema definitions

API Reference
*************

.. doxygengroup:: mcp_server
   :project: Zephyr
   :content-only:

Usage
*****

Basic Server Setup
==================

Initialize and start an MCP server:

.. code-block:: c

    #include <zephyr/kernel.h>
    #include <zephyr/logging/log.h>
    #include <zephyr/net/mcp/mcp_server.h>
    #include <zephyr/net/mcp/mcp_server_http.h>

    mcp_server_ctx_t server;

    int main(void)
    {
        int ret;

        printk("Initializing...\n\r");

        server = mcp_server_init();
        if (server == NULL) {
            printk("MCP Server initialization failed");
            return -ENOMEM;
        }

        ret = mcp_server_http_init(server);
        if (ret != 0) {
            printk("MCP HTTP Server initialization failed: %d\n\r", ret);
            return ret;
        }

        printk("Registering Tool: Hello world!...\n\r");
        ret = mcp_server_add_tool(server, &hello_world_tool);
        if (ret != 0) {
            printk("Tool registration failed.\n\r");
            return ret;
        }
        printk("Tool registered.\n\r");

        printk("Starting...\n\r");
        ret = mcp_server_start(server);
        if (ret != 0) {
            printk("MCP Server start failed: %d\n\r", ret);
            return ret;
        }

        ret = mcp_server_http_start(server);
        if (ret != 0) {
            printk("MCP HTTP Server start failed: %d\n\r", ret);
            return ret;
        }

        printk("MCP Server running...\n\r");
        return 0;
    }

Tool Registration
=================

Register a simple tool:

.. code-block:: c

    #include <zephyr/net/mcp/mcp_server.h>

    /* Tool callback functions */
    static int hello_world_tool_callback(enum mcp_tool_event_type event, const char *params, uint32_t execution_token)
    {
        if (event == MCP_TOOL_CANCEL_REQUEST)
        {
            struct mcp_tool_message cancel_ack = {
                .type = MCP_USR_TOOL_CANCEL_ACK,
                .data = NULL,
                .length = 0
            };

            mcp_server_submit_tool_message(server, &cancel_ack, execution_token);

            /* Handle cancelation */
            return 0;
        }

        struct mcp_tool_message response = {
            .type = MCP_USR_TOOL_RESPONSE,
            .data = "Hello World from tool!",
            .length = strlen("Hello World from tool!")
        };

        /* Simulate a long workload */
        k_msleep(10000);

        printk("Hello World tool executed with params: %s, token: %u\n", params ? params : "none",
            execution_token);
        mcp_server_submit_tool_message(server, &response, execution_token);
        return 0;
    }

    /* Tool definitions */
    static const struct mcp_tool_record hello_world_tool = {
        .metadata = {
                .name = "hello_world",
                .input_schema = "{\"type\":\"object\",\"properties\":{\"message\":{"
                        "\"type\":\"string\"}}}",
    #ifdef CONFIG_MCP_TOOL_DESC
                .description = "A simple hello world greeting tool",
    #endif
    #ifdef CONFIG_MCP_TOOL_TITLE
                .title = "Hello World Tool",
    #endif
    #ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
                .output_schema = "{\"type\":\"object\",\"properties\":{\"response\":{"
                        "\"type\":\"string\"}}}",
    #endif
            },
        .callback = hello_world_tool_callback
    };

Tool Removal
============

Remove a tool (fails if currently executing):

.. code-block:: c

   int ret = mcp_server_remove_tool(server, "hello_world");
   if (ret == -EBUSY) {
       printk("Tool is currently executing, try again later\n");
       /* Retry after delay or wait for execution to finish */
   } else if (ret < 0) {
       printk("Failed to remove tool: %d\n", ret);
   }

Error Handling
==============

Return errors to the client:

.. code-block:: c

   static int tool_callback(enum mcp_tool_event_type event,
                            const char *params,
                            uint32_t execution_token)
   {
       /* Validate input */
       if (!validate_params(params)) {
           char *error = "{\"type\":\"text\","
                         "\"text\":\"Invalid parameters\"}";
           struct mcp_tool_message msg = {
               .type = MCP_USR_TOOL_RESPONSE,
               .data = error,
               .is_error = true,  /* Mark as error */
           };
           return mcp_server_submit_tool_message(server, &msg, execution_token);
       }

       /* Process normally... */
   }

Transport Integration
=====================

The MCP server requires a transport layer implementation. Example skeleton:

.. code-block:: c

   #include "mcp_server_internal.h"

   static int my_transport_send(struct mcp_transport_message *msg)
   {
       /* Send msg->json_data (msg->json_len bytes) to client */
       /* IMPORTANT: This function takes ownership of msg->json_data */
       /* You must free it after sending (or on disconnect) */

       my_transport_queue_response(msg->binding, msg->json_data, msg->json_len);
       return 0;
   }

   static int my_transport_disconnect(struct mcp_transport_binding *binding)
   {
       /* Close connection and free all queued responses */
       my_transport_close(binding->context);
       return 0;
   }

   static const struct mcp_transport_ops my_ops = {
       .send = my_transport_send,
       .disconnect = my_transport_disconnect,
   };

   /* When receiving data from transport */
   void on_receive(void *client_ctx, char *json_data, size_t len)
   {
       struct mcp_transport_binding binding = {
           .ops = &my_ops,
           .context = client_ctx,
       };

       struct mcp_transport_message msg = {
           .json_data = json_data,
           .json_len = len,
           .msg_id = get_next_msg_id(),
           .binding = &binding,
       };

       enum mcp_method method;
       mcp_server_handle_request(server, &msg, &method);
   }

Performance Considerations
**************************

Stack Size
==========

Tool callbacks execute in worker thread context. If your tools need significant
stack space (e.g., for JSON parsing, large buffers), increase:

.. code-block:: kconfig

   CONFIG_MCP_REQUEST_WORKER_STACK_SIZE=8192

Worker Thread Count
===================

More workers allow more concurrent tool executions but consume more RAM:

.. code-block:: kconfig

   CONFIG_MCP_REQUEST_WORKERS=4  # For high-concurrency scenarios

For tools with blocking operations, consider using your own thread pool instead
of blocking worker threads.

Memory Management
=================

- All JSON serialization uses ``mcp_alloc()`` / ``mcp_free()``
- Transport layer owns response buffers after ``send()`` is called
- Tool responses are copied, so stack-allocated responses are safe
- Large tool responses may fragment heap - consider response size limits
- ``mcp_alloc()`` / ``mcp_free()`` are weak declared and can be redefined to use custom allocators

Sample Applications
*******************

A complete MCP server example is available at:

:zephyr_file:`samples/net/mcp_server`

The sample demonstrates:

- Server initialization and startup
- Tool registration
- HTTP transport

See Also
********

- :ref:`networking_api`
- Model Context Protocol Specification: https://modelcontextprotocol.io

API Documentation
*****************

For detailed API documentation, see:

.. toctree::
   :maxdepth: 1

   ../../api/net/mcp_server.h

