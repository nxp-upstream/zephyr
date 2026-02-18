.. _mcp_server:

MCP Server
##########

.. contents::
     :local:
     :depth: 2

Overview
********

The Model Context Protocol (MCP) is an open-source standard for connecting
AI applications to external systems, as defined by https://modelcontextprotocol.io.

MCP enables AI applications like Claude or ChatGPT to connect to:

- **Data sources** (e.g., local files, databases)
- **Tools** (e.g., search engines, calculators, device controls)
- **Workflows** (e.g., specialized prompts)

This allows AI agents to access key information and perform tasks on IoT devices.

The Zephyr MCP Server library brings MCP server functionality to MCU-based devices,
enabling them to expose tools and services to AI agents over standard network protocols.
This opens the door to new AI-driven IoT systems where embedded devices can be
directly controlled and queried by agentic AI models.

Current Implementation Status
==============================

**Phase 1 (Current Release)** includes:

- MCP Protocol Specification version 2025-11-25 compliance (core features)
- HTTP streaming transport with partial SSE support (as workaround for async limitations)
- Full tool support (registration, execution, cancellation)
- Mandatory MCP protocol methods (initialize, ping, tools/list, tools/call)
- Health monitoring with configurable timeouts
- Client lifecycle management
- Execution context tracking with token-based system

**Future Phases** will include:

- MCP Authorization and enhanced security (TLS/DTLS, client authentication)
- Full Server-Sent Events (SSE) support for streaming responses
- Additional MCP services (Resources, Prompts, Sampling, Tasks)
- MCP Session handling
- Additional transport protocols

.. note::
    The current implementation uses SSE as a workaround for HTTP server async limitations.
    Event IDs are tracked at the transport layer but not exposed to tools. SSE is not
    yet used for server-initiated notifications or streaming tool responses.

Architecture
************

The MCP server follows a layered architecture design with clear separation of concerns:

.. code-block:: none

    ┌─────────────────────────────────────────────────┐
    │          Application Layer                      │
    │  - Implements tool functionality                │
    │  - Registers/unregisters tools                  │
    │  - Handles tool callbacks (execution/cancel)    │
    │  - Application-specific business logic          │
    └──────────────────┬──────────────────────────────┘
                       │
    ┌──────────────────▼──────────────────────────────┐
    │          MCP Server Core                        │
    │  - Protocol specification implementation        │
    │  - Client Registry (lifecycle tracking)         │
    │  - Tool Registry (registered tools)             │
    │  - Execution Registry (active executions)       │
    │  - Worker thread pool management                │
    │  - Health monitoring (timeouts/cancellation)    │
    └──────────────────┬──────────────────────────────┘
                       │
    ┌──────────────────▼──────────────────────────────┐
    │          JSON Processing Layer                  │
    │  - Uses Zephyr JSON library                     │
    │  - Request deserialization                      │
    │  - Response/notification serialization          │
    │  - All MCP message types support                │
    └──────────────────┬──────────────────────────────┘
                       │
    ┌──────────────────▼──────────────────────────────┐
    │          Transport Layer                        │
    │  - Transport abstraction (operation structs)    │
    │  - HTTP transport implementation (included)     │
    │  - Response queueing with event ID ordering     │
    │  - Reference counting for safe cleanup          │
    │  - CORS header support                          │
    └─────────────────────────────────────────────────┘

Core Components
===============

MCP Server Core
---------------

The server core (``mcp_server.c``) implements the MCP protocol and manages:

- **Client Registry**: Tracks connected clients and their lifecycle states
- **Tool Registry**: Stores registered tools and metadata
- **Execution Registry**: Monitors active tool executions
- **Worker Thread Pool**: Configurable number of threads for async request processing
- **Health Monitor**: Single thread that detects and handles:
  
  - Execution timeouts (``CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS``)
  - Idle executions (``CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS``)
  - Client timeouts (``CONFIG_MCP_CLIENT_TIMEOUT_MS``)
  - Cancellation acknowledgment timeouts (``CONFIG_MCP_TOOL_CANCEL_TIMEOUT_MS``)
  - Automatically cancels stale executions
  - Sends cancellation notifications to clients

Transport Layer
---------------

The transport layer abstracts the underlying protocol through operation structs:

- **HTTP Transport**: Built on Zephyr HTTP server library
- **Partial SSE Support**: Event ID tracking for request ordering (workaround)
- **Response Queueing**: Min-heap based ordering by event ID
- **Reference Counting**: Ensures safe client cleanup during disconnection
- **Extensible**: New transports can be added without modifying core

JSON Processing
---------------

The JSON layer (``mcp_json.c``) uses Zephyr's JSON library to:

- Deserialize incoming requests
- Serialize responses and notifications
- Support all MCP message types (requests, responses, notifications, errors)
- Handle optional fields based on configuration

Memory Management
-----------------

The library uses weak allocation functions:

- ``mcp_alloc()`` / ``mcp_free()`` (defaults to ``k_malloc`` / ``k_free``)
- Applications can override with custom memory allocators
- Transport layer takes ownership of response buffers after ``send()``

Thread Model
============

The MCP server uses a multi-threaded architecture for concurrent request handling:

Main Application Thread
-----------------------

- Server initialization (``mcp_server_init()``)
- Transport initialization (``mcp_server_http_init()``)
- Tool registration/removal
- Server startup (``mcp_server_start()``)

Transport Threads
-----------------

Transport-specific (for HTTP):

- HTTP server listener threads (Zephyr HTTP server library)
- Receive incoming HTTP requests
- Call ``mcp_server_handle_request()`` to pass data to MCP core
- Queue responses with event ID ordering

Worker Threads
--------------

Configurable pool (``CONFIG_MCP_REQUEST_WORKERS``, default: 2):

- Priority: ``K_PRIO_COOP(7)``
- Stack size: ``CONFIG_MCP_REQUEST_WORKER_STACK_SIZE`` (default: 4096 bytes)
- Pull requests from shared message queue
- Process MCP protocol requests:
  
  - ``initialize``: Client initialization handshake
  - ``ping``: Heartbeat responses
  - ``tools/list``: Return available tools
  - ``tools/call``: Execute tool callbacks
  - ``notifications/initialized``: Client state transition
  - ``notifications/cancelled``: Handle cancellation requests

- Execute tool callbacks directly or allow tools to spawn their own threads

Request Queue
-------------

- Depth: ``CONFIG_MCP_MAX_CLIENTS * CONFIG_MCP_MAX_CLIENT_REQUESTS``
- Messages include client reference (with refcount), transport message ID, and parsed data
- FIFO ordering for fairness

Health Monitor Thread
---------------------

Single dedicated thread:

- Priority: ``K_PRIO_COOP(8)`` (lower priority than workers)
- Stack size: ``CONFIG_MCP_HEALTH_MONITOR_STACK_SIZE``
- Check interval: ``CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS`` (default: 1000ms)
- Monitors all execution contexts and client contexts
- Sends cancellation notifications on timeout
- Invokes tool cancellation callbacks
- Cleans up idle clients

Tool Callback Execution Context
--------------------------------

Tool callbacks execute in one of two modes:

1. **Blocking Mode**: Short-running tools execute directly in worker thread context
   
    - Blocks the worker thread until completion
    - Suitable for fast operations (< 1 second)
    - Minimizes thread overhead

2. **Async Mode**: Long-running tools spawn their own thread
   
    - Return immediately from callback
    - Worker thread returns to pool
    - Tool thread must call ``mcp_server_submit_tool_message()`` when done
    - Recommended for operations > 1 second

.. warning::
    Tool callbacks block MCP worker threads. For operations that take significant time,
    spawn your own thread to avoid degrading server responsiveness. Ensure your thread
    has adequate stack size for the tool's requirements.

.. note::
    The worker thread stack size (``CONFIG_MCP_REQUEST_WORKER_STACK_SIZE``) must be
    sufficient for tool callbacks that execute in blocking mode. Increase this value
    if your tools require more stack space, or use async mode with custom thread pools.

Client Lifecycle
================

Clients progress through well-defined states managed by the MCP server core.
Each client is stored in a fixed-size pool (``CONFIG_MCP_MAX_CLIENTS``).

Lifecycle States
----------------

Clients transition through the following states:

.. code-block:: none

    DEINITIALIZED ──┐
           ▲        │
           │        │ (allocate on first request)
           │        ▼
           │       NEW
           │        │
           │        │ (initialize request/response)
           │        ▼
           │   INITIALIZING
           │        │
           │        │ (initialized notification)
           │        ▼
           │   INITIALIZED ◄──┐
           │        │         │ (normal operation)
           │        │         │
           │        ├─────────┘
           │        │
           │        │ (timeout/disconnect)
           └────────┘

1. **DEINITIALIZED**: No client allocated in this slot

   - Initial state for all registry entries
   - Entry is available for new client allocation

2. **NEW**: Client allocated, waiting for initialization

   - Created when first request arrives (transport binding established)
   - Only accepts ``initialize`` request
   - Rejects all other requests with ``-EACCES``

3. **INITIALIZING**: Initialize response sent to client

   - Server has sent initialization response with capabilities
   - Waiting for client to confirm with ``initialized`` notification
   - Intermediate state to prevent race conditions

4. **INITIALIZED**: Client confirmed initialization

   - Ready for all MCP operations (ping, tools/list, tools/call)
   - Can have up to ``CONFIG_MCP_MAX_CLIENT_REQUESTS`` concurrent requests
   - Tracks active executions in ``active_requests[]`` array

State Transitions
-----------------

Valid transitions:

- ``NEW → INITIALIZING``: On ``initialize`` request received and response sent
- ``INITIALIZING → INITIALIZED``: On ``initialized`` notification received
- ``[Any State] → DEINITIALIZED``: On client timeout or disconnect

Invalid transitions result in ``-EPERM`` or ``-EACCES`` errors.

Reference Counting
------------------

Each client context uses atomic reference counting for safe cleanup:

- **Initial reference**: Created when client is allocated (refcount = 1)
- **Additional references**: Acquired when:
  
  - Message is queued for worker thread processing
  - Execution context is created for tool call
  - Health monitor is checking client state

- **Reference release**: When message processing completes or execution finishes
- **Cleanup**: When refcount reaches 0, client is fully removed and slot is freed

This mechanism ensures:

- No use-after-free when clients disconnect during request processing
- Transport can safely queue responses until client is fully cleaned up
- Worker threads can safely access client context during async operations

.. important::
   The MCP core marks clients as ``DEINITIALIZED`` immediately on disconnect/timeout,
   preventing new references. Actual cleanup happens when the last reference is released.

Active Request Tracking
-----------------------

Each client tracks active tool executions:

- Array: ``active_requests[CONFIG_MCP_MAX_CLIENT_REQUESTS]``
- Pointers to execution contexts in the execution registry
- Incremented on ``tools/call`` request
- Decremented when tool submits response or acknowledges cancellation
- Used to enforce per-client concurrency limits

Client Health Monitoring
-------------------------

The health monitor thread checks each client:

- **Last message timestamp**: Updated on any request/response
- **Idle timeout**: ``CONFIG_MCP_CLIENT_TIMEOUT_MS`` (default: 300000ms = 5 minutes)
- **Action on timeout**: Client marked as disconnected, all executions cancelled

Tool Execution Flow
===================

Each tool execution is tracked through its lifecycle with a unique execution token.

Execution States
----------------

Tool executions progress through these states:

.. code-block:: none

    FREE ────┐
      ▲      │
      │      │ (tools/call request)
      │      ▼
      │   ACTIVE ◄──────┐
      │      │          │ (ping messages)
      │      │          │
      │      ├──────────┘
      │      │
      │      ├───► CANCELED ───┐
      │      │    (timeout or     │
      │      │     client request)│
      │      │                    │
      │      │ (tool response)    │ (cancel ack)
      │      ▼                    ▼
      └─── FINISHED ◄─────────────┘

1. **FREE**: Execution slot available

   - Default state in execution registry
   - Can be allocated for new tool call

2. **ACTIVE**: Tool execution in progress

   - Created when ``tools/call`` request is processed
   - Execution token assigned (currently based on message ID, will use UUID in security phase)
   - Tool callback is invoked
   - Monitored for execution timeout and idle timeout

3. **CANCELED**: Cancellation requested

   - Triggered by client ``cancelled`` notification OR health monitor timeout
   - Tool receives ``MCP_TOOL_CANCEL_REQUEST`` event
   - Cancellation timestamp recorded
   - Monitored for cancellation acknowledgment timeout

4. **FINISHED**: Execution completed

   - Tool submitted response (``MCP_USR_TOOL_RESPONSE``)
   - OR tool acknowledged cancellation (``MCP_USR_TOOL_CANCEL_ACK``)
   - Execution context cleaned up and slot freed

Complete Execution Flow
-----------------------

Normal execution path:

1. **Client Request Arrival**:
   
   - Transport layer receives HTTP request with JSON-RPC message
   - Transport validates headers, creates ``mcp_transport_message``
   - Calls ``mcp_server_handle_request()``

2. **Request Parsing and Validation**:
   
   - JSON message parsed into ``struct mcp_message``
   - Method extracted and validated
   - Client context looked up by transport binding
   - Client state validated (must be ``INITIALIZED`` for tool calls)

3. **Request Queueing**:
   
   - For ``tools/call``: Message queued for worker thread
   - Client reference count incremented
   - ``initialize`` requests handled immediately (special case)

4. **Worker Processing**:
   
   - Worker thread dequeues request from message queue
   - For ``tools/call``:
     
     - Tool looked up in tool registry by name
     - Tool refcount incremented (prevents removal during execution)
     - Execution context created with unique token
     - Client's ``active_request_count`` incremented
     - Execution context added to client's ``active_requests[]`` array

5. **Tool Callback Invocation**:
   
   - Worker calls ``tool->callback(MCP_TOOL_CALL_REQUEST, params, token)``
   - Tool can either:
     
     - **Block**: Process and call ``mcp_server_submit_tool_message()`` before returning
     - **Async**: Spawn thread, return immediately, thread calls ``mcp_server_submit_tool_message()`` later

6. **Tool Response Submission**:
   
   - Tool calls ``mcp_server_submit_tool_message()`` with ``MCP_USR_TOOL_RESPONSE``
   - Execution context looked up by token
   - If execution was canceled, response is dropped with warning
   - Response serialized to JSON
   - Sent via transport layer's ``send()`` operation

7. **Cleanup**:
   
   - Execution state set to ``FINISHED``
   - Removed from client's ``active_requests[]`` array
   - Client's ``active_request_count`` decremented
   - Tool refcount decremented
   - Execution context freed and returned to pool
   - Client reference released

Cancellation Flow
-----------------

Cancellation can be triggered by:

1. **Client Request**: ``cancelled`` notification with request ID
2. **Execution Timeout**: Health monitor detects ``CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS`` exceeded
3. **Idle Timeout**: Health monitor detects ``CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS`` exceeded

Cancellation process:

1. Execution state changed to ``CANCELED``
2. Cancellation timestamp recorded
3. Tool callback invoked with ``MCP_TOOL_CANCEL_REQUEST`` event
4. Tool should:
   
   - Stop processing gracefully
   - Submit ``MCP_USR_TOOL_CANCEL_ACK`` message
   - Clean up any resources

5. If tool doesn't acknowledge within ``CONFIG_MCP_TOOL_CANCEL_TIMEOUT_MS``:
   
   - Health monitor logs error
   - Execution remains in ``CANCELED`` state
   - Cleanup happens when tool finally responds or server restarts

Execution Token System
----------------------

Each tool execution receives a unique token for tracking:

- **Current Implementation**: Token based on transport message ID
- **Future (Security Phase)**: UUID-based tokens for better security
- **Purpose**:
  
  - Correlates responses with original requests
  - Allows tools to submit responses from any thread
  - Enables execution state queries
  - Prevents response spoofing (with UUID implementation)

Execution Monitoring
--------------------

The health monitor thread continuously checks all active executions:

**Execution Timeout Check**:

.. code-block:: c

  if (uptime - start_timestamp > CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS) {
      /* Send cancellation notification to client */
      /* Invoke tool's cancel callback */
      /* Transition to CANCELED state */
  }

  if (uptime - last_message_timestamp > CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS) {
      /* Send cancellation notification to client */
      /* Invoke tool's cancel callback */
      /* Transition to CANCELED state */
  }

Tools can prevent idle timeout by sending ping messages:

struct mcp_tool_message ping = {
    .type = MCP_USR_TOOL_PING,
    .data = NULL,
    .length = 0
};
mcp_server_submit_tool_message(server, &ping, execution_token);

.. note::
  Ping messages update last_message_timestamp but do not send any data to the client.
  They are purely for keeping the execution alive during long-running operations.

Configuration
*************

The MCP server is configured through Kconfig options. All options are prefixed with ``CONFIG_MCP_``.

Core Configuration
==================

.. code-block:: kconfig

   CONFIG_MCP_SERVER
      Enable MCP Server support
      Type: bool
      Default: n

   CONFIG_MCP_SERVER_HTTP
      Enable HTTP transport for MCP Server
      Type: bool
      Default: n
      Depends on: CONFIG_MCP_SERVER

Client and Execution Limits
============================

.. code-block:: kconfig

   CONFIG_MCP_MAX_CLIENTS
      Maximum number of concurrent MCP clients
      Type: int
      Default: 4
      Range: 1-32
      
      Each client consumes memory for tracking state, active requests,
      and execution contexts. Increase if you need more concurrent AI agents.

   CONFIG_MCP_MAX_CLIENT_REQUESTS
      Maximum concurrent requests per client
      Type: int
      Default: 10
      Range: 1-64
      
      Limits parallel tool executions from a single client. The total
      execution registry size is MAX_CLIENTS * MAX_CLIENT_REQUESTS.

   CONFIG_MCP_MAX_TOOLS
      Maximum number of tools that can be registered
      Type: int
      Default: 16
      Range: 1-128
      
      Static array size for tool registry. Increase if your application
      provides many tools to AI agents.

Tool Metadata Sizes
===================

.. code-block:: kconfig

   CONFIG_MCP_TOOL_NAME_MAX_LEN
      Maximum length of tool name (including null terminator)
      Type: int
      Default: 64
      Range: 8-256

   CONFIG_MCP_TOOL_SCHEMA_MAX_LEN
      Maximum length of tool input schema JSON (including null terminator)
      Type: int
      Default: 512
      Range: 64-4096
      
      Input schema is a JSON Schema definition for tool parameters.
      Increase if your tools have complex parameter structures.

   CONFIG_MCP_TOOL_DESC
      Enable tool description field
      Type: bool
      Default: y
      
      Allows tools to provide human-readable descriptions to AI agents.

   CONFIG_MCP_TOOL_DESC_MAX_LEN
      Maximum length of tool description (including null terminator)
      Type: int
      Default: 256
      Range: 32-1024
      Depends on: CONFIG_MCP_TOOL_DESC

   CONFIG_MCP_TOOL_TITLE
      Enable tool title field
      Type: bool
      Default: n
      
      Optional alternative name for the tool.

   CONFIG_MCP_TOOL_OUTPUT_SCHEMA
      Enable tool output schema field
      Type: bool
      Default: n
      
      Allows tools to specify expected output format as JSON Schema.
      Future feature for enhanced type safety.

   CONFIG_MCP_TOOL_OUTPUT_SCHEMA_MAX_LEN
      Maximum length of tool output schema (including null terminator)
      Type: int
      Default: 512
      Range: 64-4096
      Depends on: CONFIG_MCP_TOOL_OUTPUT_SCHEMA

Thread Configuration
====================

.. code-block:: kconfig

   CONFIG_MCP_REQUEST_WORKERS
      Number of worker threads for processing MCP requests
      Type: int
      Default: 2
      Range: 1-16
      
      More workers allow greater concurrency but consume more stack memory.
      Each worker can process one tool execution at a time.

   CONFIG_MCP_REQUEST_WORKER_STACK_SIZE
      Stack size for each worker thread (bytes)
      Type: int
      Default: 4096
      Range: 2048-16384
      
      Must be sufficient for:
      - MCP request processing overhead
      - JSON parsing/serialization
      - Tool callback execution (if blocking mode)
      
      Increase if tools perform complex operations or use large stack buffers.

   CONFIG_MCP_HEALTH_MONITOR_STACK_SIZE
      Stack size for health monitor thread (bytes)
      Type: int
      Default: 2048
      Range: 1024-8192
      
      Health monitor has minimal stack requirements. Default is usually sufficient.

Timeout Configuration
=====================

.. code-block:: kconfig

   CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS
      Interval between health monitor checks (milliseconds)
      Type: int
      Default: 1000
      Range: 100-10000
      
      How often the health monitor wakes up to check for timeouts.
      Lower values provide faster timeout detection but increase CPU usage.

   CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS
      Maximum execution time for a tool (milliseconds)
      Type: int
      Default: 60000
      Range: 1000-600000
      
      Tools exceeding this duration are automatically cancelled.
      Set based on longest expected tool execution time.

   CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS
      Idle timeout for tool execution (milliseconds)
      Type: int
      Default: 10000
      Range: 1000-300000
      
      If no ping or response within this time, execution is cancelled.
      Tools doing long computation should send periodic pings.

   CONFIG_MCP_TOOL_CANCEL_TIMEOUT_MS
      Timeout for tool cancellation acknowledgment (milliseconds)
      Type: int
      Default: 5000
      Range: 500-60000
      
      If tool doesn't acknowledge cancellation within this time,
      health monitor logs an error. Execution remains in CANCELED state.

   CONFIG_MCP_CLIENT_TIMEOUT_MS
      Client idle timeout (milliseconds)
      Type: int
      Default: 300000
      Range: 10000-3600000
      
      Clients with no activity for this duration are disconnected.
      All their active executions are cancelled.

HTTP Transport Configuration
=============================

.. code-block:: kconfig

   CONFIG_MCP_HTTP_PORT
      HTTP server port for MCP
      Type: int
      Default: 8080
      Range: 1-65535

   CONFIG_MCP_HTTP_MAX_CONTENT_LENGTH
      Maximum HTTP request body size (bytes)
      Type: int
      Default: 4096
      Range: 512-65536
      
      Must accommodate largest expected MCP request (tool calls with parameters).

   CONFIG_MCP_HTTP_RESPONSE_QUEUE_SIZE
      Size of HTTP response queue per client
      Type: int
      Default: 16
      Range: 4-64
      
      Responses are queued with event ID ordering for SSE compatibility.
      Increase if clients send many rapid concurrent requests.

Memory Configuration
====================

.. code-block:: kconfig

   CONFIG_MCP_USE_HEAP
      Use heap allocation for dynamic memory
      Type: bool
      Default: y
      
      If disabled, you must provide custom mcp_alloc()/mcp_free() implementations.

.. warning::
   The total execution registry size is ``CONFIG_MCP_MAX_CLIENTS * CONFIG_MCP_MAX_CLIENT_REQUESTS``.
   Each execution context consumes memory for state tracking. Ensure sufficient heap is available.

.. note::
   The request queue depth is automatically calculated as:
   ``CONFIG_MCP_MAX_CLIENTS * CONFIG_MCP_MAX_CLIENT_REQUESTS``
   
   This ensures all possible concurrent executions can be queued.

API Reference
*************

.. doxygengroup:: mcp_server
    :project: Zephyr
    :content-only:

Usage Guide
***********

Server Initialization
=====================

Initialize and start the MCP server:

.. code-block:: c

    #include <zephyr/net/mcp/mcp_server.h>
    #include <zephyr/net/mcp/mcp_server_http.h>

    mcp_server_ctx_t server;

    int main(void)
    {
        int ret;

        /* Initialize MCP server core */
        server = mcp_server_init();
        if (server == NULL) {
            return -ENOMEM;
        }

        /* Initialize HTTP transport layer */
        ret = mcp_server_http_init(server);
        if (ret != 0) {
            return ret;
        }

        /* Register tools (see Tool Registration section) */
        ret = mcp_server_add_tool(server, &my_tool);
        if (ret != 0) {
            return ret;
        }

        /* Start worker threads and health monitor */
        ret = mcp_server_start(server);
        if (ret != 0) {
            return ret;
        }

        /* Start HTTP listener */
        ret = mcp_server_http_start(server);
        if (ret != 0) {
            return ret;
        }

        return 0;
    }

Tool Registration
=================

Define a tool and register it with the server:

.. code-block:: c

    static int my_tool_callback(enum mcp_tool_event_type event,
                                const char *params,
                                uint32_t execution_token)
    {
        if (event == MCP_TOOL_CANCEL_REQUEST) {
            /* Handle cancellation */
            struct mcp_tool_message ack = {
                .type = MCP_USR_TOOL_CANCEL_ACK,
            };
            mcp_server_submit_tool_message(server, &ack, execution_token);
            return 0;
        }

        /* Process tool request and generate response */
        char *result = "{\"type\":\"text\",\"text\":\"Result data\"}";
        struct mcp_tool_message response = {
            .type = MCP_USR_TOOL_RESPONSE,
            .data = result,
            .is_error = false
        };

        return mcp_server_submit_tool_message(server, &response, execution_token);
    }

    static const struct mcp_tool_record my_tool = {
        .metadata = {
            .name = "my_tool",
            .input_schema = "{\"type\":\"object\",\"properties\":{}}",
    #ifdef CONFIG_MCP_TOOL_DESC
            .description = "Tool description for AI agents",
    #endif
        },
        .callback = my_tool_callback
    };

    /* Register the tool */
    ret = mcp_server_add_tool(server, &my_tool);

.. note::
    Tool callbacks are executed in worker thread context. For operations taking
    longer than ~1 second, spawn a separate thread to avoid blocking workers.

Tool Removal
============

Remove a tool dynamically:

.. code-block:: c

    int ret = mcp_server_remove_tool(server, "my_tool");
    if (ret == -EBUSY) {
        /* Tool is currently executing, retry later */
    } else if (ret == -ENOENT) {
        /* Tool not found */
    }

.. warning::
    Tools cannot be removed while actively executing. Check for ``-EBUSY`` and retry.

Submitting Tool Responses
==========================

Tools must submit exactly one response per execution:

**Success Response:**

.. code-block:: c

    struct mcp_tool_message msg = {
        .type = MCP_USR_TOOL_RESPONSE,
        .data = "{\"type\":\"text\",\"text\":\"Success\"}",
        .is_error = false
    };
    mcp_server_submit_tool_message(server, &msg, execution_token);

**Error Response:**

.. code-block:: c

    struct mcp_tool_message msg = {
        .type = MCP_USR_TOOL_RESPONSE,
        .data = "{\"type\":\"text\",\"text\":\"Error occurred\"}",
        .is_error = true
    };
    mcp_server_submit_tool_message(server, &msg, execution_token);

**Cancellation Acknowledgment:**

.. code-block:: c

    struct mcp_tool_message ack = {
        .type = MCP_USR_TOOL_CANCEL_ACK,
    };
    mcp_server_submit_tool_message(server, &ack, execution_token);

Long-Running Operations
=======================

For operations exceeding ``CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS``, send periodic pings:

.. code-block:: c

    /* During long operation */
    struct mcp_tool_message ping = {
        .type = MCP_USR_TOOL_PING,
    };
    mcp_server_submit_tool_message(server, &ping, execution_token);

.. tip::
    Ping messages update the idle timer without sending data to the client.
    They prevent automatic cancellation during long computations.

Checking Execution State
=========================

Query whether an execution has been cancelled:

.. code-block:: c

    bool is_canceled;
    int ret;

    ret = mcp_server_is_execution_canceled(server, execution_token, &is_canceled);
    if (ret == 0 && is_canceled) {
        /* Stop processing and cleanup */
    }

.. note::
    Prefer handling ``MCP_TOOL_CANCEL_REQUEST`` events over polling this API.
    Events are more efficient and don't require mutex operations.

Response Format
===============

Tool responses must follow the MCP content format:

**Text Content:**

.. code-block:: json

    {
        "type": "text",
        "text": "Response data here"
    }

**Example with structured data:**

.. code-block:: c

    char response[512];
    snprintf(response, sizeof(response),
              "{\"type\":\"text\","
              "\"text\":\"{\\\"temp\\\":23.5,\\\"unit\\\":\\\"C\\\"}\"}");

Configuration
*************

Core Settings
=============

.. code-block:: kconfig

    CONFIG_MCP_SERVER=y                     # Enable MCP server
    CONFIG_MCP_SERVER_HTTP=y                # Enable HTTP transport
    CONFIG_MCP_MAX_CLIENTS=4                # Max concurrent clients
    CONFIG_MCP_MAX_CLIENT_REQUESTS=10       # Max requests per client
    CONFIG_MCP_MAX_TOOLS=16                 # Max registered tools

Thread Configuration
====================

.. code-block:: kconfig

    CONFIG_MCP_REQUEST_WORKERS=2                  # Worker thread count
    CONFIG_MCP_REQUEST_WORKER_STACK_SIZE=4096     # Worker stack size
    CONFIG_MCP_HEALTH_MONITOR_STACK_SIZE=2048     # Monitor stack size

Timeout Configuration
=====================

.. code-block:: kconfig

    CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS=60000      # Max execution time
    CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS=10000      # Idle timeout (use pings)
    CONFIG_MCP_TOOL_CANCEL_TIMEOUT_MS=5000     # Cancel ack timeout
    CONFIG_MCP_CLIENT_TIMEOUT_MS=300000        # Client idle timeout
    CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS=1000   # Monitor check interval

HTTP Transport
==============

.. code-block:: kconfig

    CONFIG_MCP_HTTP_PORT=8080                      # Server port
    CONFIG_MCP_HTTP_MAX_CONTENT_LENGTH=4096        # Max request size
    CONFIG_MCP_HTTP_RESPONSE_QUEUE_SIZE=16         # Response queue depth

Tool Metadata
=============

.. code-block:: kconfig

    CONFIG_MCP_TOOL_NAME_MAX_LEN=64            # Tool name length
    CONFIG_MCP_TOOL_SCHEMA_MAX_LEN=512         # Schema JSON length
    CONFIG_MCP_TOOL_DESC=y                     # Enable descriptions
    CONFIG_MCP_TOOL_DESC_MAX_LEN=256           # Description length

Performance Tuning
******************

Stack Sizing
============

Calculate worker stack requirements:

.. code-block:: none

    WORKER_STACK = Tool callback stack + MCP overhead (~2KB)

For tools using large buffers or deep call stacks, increase:

.. code-block:: kconfig

    CONFIG_MCP_REQUEST_WORKER_STACK_SIZE=8192

Worker Thread Count
===================

Tune based on concurrency needs:

- **Low concurrency** (1-2 clients): ``CONFIG_MCP_REQUEST_WORKERS=1``
- **Medium concurrency** (2-4 clients): ``CONFIG_MCP_REQUEST_WORKERS=2`` (default)
- **High concurrency** (4+ clients): ``CONFIG_MCP_REQUEST_WORKERS=4``

Total memory impact:

.. code-block:: none

    Worker memory = WORKER_COUNT × WORKER_STACK_SIZE

Memory Usage
============

Total execution registry size:

.. code-block:: none

    Execution slots = MAX_CLIENTS × MAX_CLIENT_REQUESTS

Each slot uses ~100 bytes. For default settings (4 clients × 10 requests):

.. code-block:: none

    Execution registry ≈ 4KB

Custom Memory Allocators
========================

Override default allocator by redefining weak symbols:

.. code-block:: c

    void *mcp_alloc(size_t size)
    {
        return my_custom_alloc(size);
    }

    void mcp_free(void *ptr)
    {
        my_custom_free(ptr);
    }

Transport Layer Ownership
==========================

After ``transport->ops->send()`` is called:

- Transport layer owns the JSON buffer
- Transport must free buffer after transmission
- Transport must free all queued buffers on disconnect

Example Configurations
======================

**Resource Constrained:**

.. code-block:: kconfig

    CONFIG_MCP_MAX_CLIENTS=2
    CONFIG_MCP_MAX_CLIENT_REQUESTS=4
    CONFIG_MCP_REQUEST_WORKERS=1
    CONFIG_MCP_REQUEST_WORKER_STACK_SIZE=2048
    CONFIG_MCP_MAX_TOOLS=8

**High Performance:**

.. code-block:: kconfig

    CONFIG_MCP_MAX_CLIENTS=8
    CONFIG_MCP_MAX_CLIENT_REQUESTS=16
    CONFIG_MCP_REQUEST_WORKERS=4
    CONFIG_MCP_REQUEST_WORKER_STACK_SIZE=8192
    CONFIG_MCP_MAX_TOOLS=32

**Long-Running Operations:**

.. code-block:: kconfig

    CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS=300000
    CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS=60000
    CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS=5000

Sample Application
******************

For application examples see:

:zephyr_file:`samples/net/mcp_server`

See Also
********

- :ref:`networking_api` - Zephyr networking APIs
- https://modelcontextprotocol.io - MCP specification
- :zephyr_file:`samples/net/mcp_server` - Complete examples
