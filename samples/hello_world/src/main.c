#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/data/json.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Use LOG_LEVEL_DBG for verbose logging, INF for important messages only */
LOG_MODULE_REGISTER(mcp_parser, LOG_LEVEL_DBG);

/* Configuration - would come from Kconfig in production */
#ifndef CONFIG_MCP_TOOL_NAME_MAX_LEN
#define CONFIG_MCP_TOOL_NAME_MAX_LEN 32
#endif

/* ============================================================================
 * MESSAGE TYPE ENUMERATION
 * ============================================================================ */

typedef enum {
    MCP_MSG_UNKNOWN = 0,
    MCP_MSG_REQUEST,
    MCP_MSG_RESPONSE,
    MCP_MSG_ERROR,
    MCP_MSG_NOTIFICATION
} mcp_msg_class_t;

/* ============================================================================
 * PARSER ERROR CODES
 * ============================================================================ */

typedef enum {
    PARSER_OK = 0,
    PARSER_ERR_NULL_INPUT = -1,
    PARSER_ERR_INVALID_JSON = -2,
    PARSER_ERR_BUFFER_TOO_SMALL = -3,
    PARSER_ERR_INVALID_FORMAT = -4,
    PARSER_ERR_UNSUPPORTED = -5
} parser_status_t;

/* ============================================================================
 * MCP MESSAGE STRUCTURE (Internal Representation)
 * ============================================================================ */

struct mcp_message {
    /* Message classification */
    mcp_msg_class_t msg_class;
    
    /* JSON-RPC fields */
    int id;                    /* Request/Response ID (0 for notifications) */
    char method[64];          /* Method name for requests/notifications */
    
    /* Tool-specific field (for tools/call) */
    char tool_name[CONFIG_MCP_TOOL_NAME_MAX_LEN];  /* Tool name extracted from params */
    
    /* Payload - raw JSON strings */
    char params[512];         /* Parameters (for requests/notifications) */
    char result[512];         /* Result (for responses) */
    char error[256];          /* Error details (for error responses) */
    
    /* Metadata */
    bool has_id;             /* True if ID was present */
    bool has_params;         /* True if params were present */
    bool has_result;         /* True if result was present */
    bool has_error;          /* True if error was present */
    bool has_tool_name;      /* True if tool name was extracted */
};

/* ============================================================================
 * HELPER FUNCTION - Extract Tool Name from Params
 * ============================================================================ */

static void extract_tool_name(const char *params_json, char *tool_name, size_t max_len)
{
    if (!params_json || !tool_name) return;
    
    /* Clear the output */
    memset(tool_name, 0, max_len);
    
    /* Look for "name" field in params */
    const char *name_str = strstr(params_json, "\"name\"");
    if (name_str) {
        const char *colon = strchr(name_str, ':');
        if (colon) {
            /* Skip whitespace and quotes */
            const char *start = colon + 1;
            while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '"')) {
                start++;
            }
            
            /* Find end quote */
            const char *end = start;
            while (*end && *end != '"') {
                end++;
            }
            
            /* Copy the name */
            size_t len = end - start;
            if (len > 0 && len < max_len) {
                memcpy(tool_name, start, len);
                tool_name[len] = '\0';
            }
        }
    }
}

/* ============================================================================
 * PARSE FUNCTION - JSON String to Structure
 * ============================================================================ */

/**
 * Parse a JSON-RPC message into internal structure
 * 
 * @param json_input   Input JSON string (null-terminated)
 * @param msg          Output message structure (must be pre-allocated)
 * @return             PARSER_OK on success, error code on failure
 */
parser_status_t mcp_parse(const char *json_input, struct mcp_message *msg)
{
    /* Validate inputs */
    if (!json_input || !msg) {
        LOG_ERR("Parse: NULL input");
        return PARSER_ERR_NULL_INPUT;
    }
    
    /* Clear output structure */
    memset(msg, 0, sizeof(struct mcp_message));
    
    /* Basic validation - check if it looks like JSON */
    const char *start = json_input;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n')) {
        start++;
    }
    if (*start != '{') {
        LOG_ERR("Parse: Not a JSON object");
        return PARSER_ERR_INVALID_JSON;
    }
    
    /* Check for JSON-RPC version (we don't store it, just validate) */
    const char *jsonrpc = strstr(json_input, "\"jsonrpc\"");
    if (!jsonrpc) {
        LOG_ERR("Parse: Missing jsonrpc field");
        return PARSER_ERR_INVALID_FORMAT;
    }
    
    /* Find and validate version is "2.0" */
    const char *version = strstr(jsonrpc, "\"2.0\"");
    if (!version || (version - jsonrpc) > 50) {  /* Should be close */
        LOG_ERR("Parse: Invalid JSON-RPC version");
        return PARSER_ERR_INVALID_FORMAT;
    }
    
    /* Extract ID if present */
    const char *id_str = strstr(json_input, "\"id\"");
    if (id_str) {
        const char *colon = strchr(id_str, ':');
        if (colon) {
            msg->id = atoi(colon + 1);
            msg->has_id = true;
        }
    }
    
    /* Extract method if present (indicates request/notification) */
    const char *method_str = strstr(json_input, "\"method\"");
    if (method_str) {
        const char *colon = strchr(method_str, ':');
        if (colon) {
            const char *quote_start = strchr(colon, '"');
            if (quote_start) {
                quote_start++;
                const char *quote_end = strchr(quote_start, '"');
                if (quote_end) {
                    size_t len = quote_end - quote_start;
                    if (len < sizeof(msg->method)) {
                        memcpy(msg->method, quote_start, len);
                        msg->method[len] = '\0';
                    }
                }
            }
        }
    }
    
    /* Extract params if present */
    const char *params_str = strstr(json_input, "\"params\"");
    if (params_str) {
        const char *colon = strchr(params_str, ':');
        if (colon) {
            colon++;
            /* Skip whitespace */
            while (*colon == ' ' || *colon == '\t' || *colon == '\n') {
                colon++;
            }
            
            /* Find the end of params object/array */
            char start_char = *colon;
            char end_char = (start_char == '{') ? '}' : 
                           (start_char == '[') ? ']' : '\0';
            
            if (end_char) {
                int depth = 1;
                const char *p = colon + 1;
                const char *end = p;
                
                while (*p && depth > 0) {
                    if (*p == start_char) depth++;
                    else if (*p == end_char) {
                        depth--;
                        if (depth == 0) {
                            end = p + 1;
                            break;
                        }
                    }
                    p++;
                }
                
                size_t len = end - colon;
                if (len > 0 && len < sizeof(msg->params)) {
                    memcpy(msg->params, colon, len);
                    msg->params[len] = '\0';
                    msg->has_params = true;
                    
                    /* If this is a tools/call, extract the tool name */
                    if (strcmp(msg->method, "tools/call") == 0) {
                        extract_tool_name(msg->params, msg->tool_name, CONFIG_MCP_TOOL_NAME_MAX_LEN);
                        if (msg->tool_name[0]) {
                            msg->has_tool_name = true;
                            LOG_DBG("Extracted tool name: %s", msg->tool_name);
                        }
                    }
                }
            }
        }
    }
    
    /* Extract result if present (indicates response) */
    const char *result_str = strstr(json_input, "\"result\"");
    if (result_str) {
        const char *colon = strchr(result_str, ':');
        if (colon) {
            colon++;
            /* Skip whitespace */
            while (*colon == ' ' || *colon == '\t' || *colon == '\n') {
                colon++;
            }
            
            /* Copy everything until we find a top-level comma or closing brace */
            int depth = 0;
            const char *p = colon;
            const char *end = p;
            bool in_string = false;
            
            while (*p) {
                if (*p == '"' && *(p-1) != '\\') {
                    in_string = !in_string;
                }
                if (!in_string) {
                    if (*p == '{' || *p == '[') depth++;
                    else if (*p == '}' || *p == ']') {
                        depth--;
                        if (depth < 0) {
                            end = p;
                            break;
                        }
                    } else if (*p == ',' && depth == 0) {
                        end = p;
                        break;
                    }
                }
                p++;
            }
            if (*p == '\0') end = p;
            
            size_t len = end - colon;
            if (len > 0 && len < sizeof(msg->result)) {
                memcpy(msg->result, colon, len);
                msg->result[len] = '\0';
                msg->has_result = true;
            }
        }
    }
    
    /* Extract error if present (indicates error response) */
    const char *error_str = strstr(json_input, "\"error\"");
    if (error_str) {
        const char *colon = strchr(error_str, ':');
        if (colon) {
            colon++;
            /* Skip whitespace */
            while (*colon == ' ' || *colon == '\t' || *colon == '\n') {
                colon++;
            }
            
            /* Find the end of error object */
            if (*colon == '{') {
                int depth = 1;
                const char *p = colon + 1;
                const char *end = p;
                
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') {
                        depth--;
                        if (depth == 0) {
                            end = p + 1;
                            break;
                        }
                    }
                    p++;
                }
                
                size_t len = end - colon;
                if (len > 0 && len < sizeof(msg->error)) {
                    memcpy(msg->error, colon, len);
                    msg->error[len] = '\0';
                    msg->has_error = true;
                }
            }
        }
    }
    
    /* Determine message class based on what we found */
    if (msg->has_error) {
        msg->msg_class = MCP_MSG_ERROR;
    } else if (msg->has_result) {
        msg->msg_class = MCP_MSG_RESPONSE;
    } else if (msg->method[0]) {
        msg->msg_class = msg->has_id ? MCP_MSG_REQUEST : MCP_MSG_NOTIFICATION;
    } else {
        msg->msg_class = MCP_MSG_UNKNOWN;
    }
    
    /* Use debug level for routine parsing */
    LOG_DBG("Parsed %s: id=%d, method=%s", 
            msg->msg_class == MCP_MSG_REQUEST ? "request" :
            msg->msg_class == MCP_MSG_RESPONSE ? "response" :
            msg->msg_class == MCP_MSG_ERROR ? "error" :
            msg->msg_class == MCP_MSG_NOTIFICATION ? "notification" : "unknown",
            msg->id, msg->method[0] ? msg->method : "(none)");
    
    return PARSER_OK;
}

/* ============================================================================
 * SERIALIZE FUNCTION - Structure to JSON String
 * ============================================================================ */

/**
 * Serialize internal message structure to JSON-RPC format
 * 
 * @param msg          Input message structure
 * @param json_output  Output buffer for JSON string
 * @param buffer_size  Size of output buffer
 * @param actual_size  Actual size of generated JSON (output)
 * @return             PARSER_OK on success, error code on failure
 */
parser_status_t mcp_serialize(const struct mcp_message *msg,
                              char *json_output,
                              size_t buffer_size,
                              size_t *actual_size)
{
    /* Validate inputs */
    if (!msg || !json_output || !actual_size) {
        LOG_ERR("Serialize: NULL input");
        return PARSER_ERR_NULL_INPUT;
    }
    
    if (buffer_size < 64) {  /* Minimum for simplest message */
        LOG_ERR("Serialize: Buffer too small");
        return PARSER_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Clear output buffer */
    memset(json_output, 0, buffer_size);
    
    /* Start building JSON */
    int written = 0;
    
    /* Always add JSON-RPC version */
    written = snprintf(json_output, buffer_size, "{\"jsonrpc\":\"2.0\"");
    
    if (written < 0 || written >= buffer_size) {
        LOG_ERR("Serialize: Buffer overflow");
        return PARSER_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Add ID if present (not for notifications) */
    if (msg->has_id || msg->msg_class != MCP_MSG_NOTIFICATION) {
        written += snprintf(json_output + written, buffer_size - written,
                           ",\"id\":%d", msg->id);
    }
    
    /* Add fields based on message class */
    switch (msg->msg_class) {
        case MCP_MSG_REQUEST:
        case MCP_MSG_NOTIFICATION:
            /* Add method */
            if (!msg->method[0]) {
                LOG_ERR("Serialize: Request/notification without method");
                return PARSER_ERR_INVALID_FORMAT;
            }
            written += snprintf(json_output + written, buffer_size - written,
                               ",\"method\":\"%s\"", msg->method);
            
            /* Add params if present */
            if (msg->has_params && msg->params[0]) {
                written += snprintf(json_output + written, buffer_size - written,
                                   ",\"params\":%s", msg->params);
            }
            break;
            
        case MCP_MSG_RESPONSE:
            /* Add result */
            if (!msg->has_result) {
                /* Result is mandatory for response, use null if not present */
                written += snprintf(json_output + written, buffer_size - written,
                                   ",\"result\":null");
            } else {
                written += snprintf(json_output + written, buffer_size - written,
                                   ",\"result\":%s", msg->result);
            }
            break;
            
        case MCP_MSG_ERROR:
            /* Add error */
            if (!msg->has_error || !msg->error[0]) {
                LOG_ERR("Serialize: Error response without error details");
                return PARSER_ERR_INVALID_FORMAT;
            }
            written += snprintf(json_output + written, buffer_size - written,
                               ",\"error\":%s", msg->error);
            break;
            
        default:
            LOG_ERR("Serialize: Unknown message class");
            return PARSER_ERR_UNSUPPORTED;
    }
    
    /* Close JSON object */
    written += snprintf(json_output + written, buffer_size - written, "}");
    
    if (written < 0 || written >= buffer_size) {
        LOG_ERR("Serialize: Buffer overflow");
        return PARSER_ERR_BUFFER_TOO_SMALL;
    }
    
    *actual_size = written;
    
    /* Use debug level for routine serialization */
    LOG_DBG("Serialized %s: %zu bytes", 
            msg->msg_class == MCP_MSG_REQUEST ? "request" :
            msg->msg_class == MCP_MSG_RESPONSE ? "response" :
            msg->msg_class == MCP_MSG_ERROR ? "error" :
            msg->msg_class == MCP_MSG_NOTIFICATION ? "notification" : "unknown",
            *actual_size);
    
    return PARSER_OK;
}

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void print_message(const struct mcp_message *msg)
{
    printk("\n--- Parsed Message ---\n");
    printk("Class: %s\n", 
           msg->msg_class == MCP_MSG_REQUEST ? "REQUEST" :
           msg->msg_class == MCP_MSG_RESPONSE ? "RESPONSE" :
           msg->msg_class == MCP_MSG_ERROR ? "ERROR" :
           msg->msg_class == MCP_MSG_NOTIFICATION ? "NOTIFICATION" : "UNKNOWN");
    
    if (msg->has_id) {
        printk("ID: %d\n", msg->id);
    }
    
    if (msg->method[0]) {
        printk("Method: %s\n", msg->method);
    }
    
    if (msg->has_tool_name) {
        printk("Tool Name: %s\n", msg->tool_name);
    }
    
    if (msg->has_params) {
        printk("Params: %s\n", msg->params);
    }
    
    if (msg->has_result) {
        printk("Result: %s\n", msg->result);
    }
    
    if (msg->has_error) {
        printk("Error: %s\n", msg->error);
    }
    printk("-------------------\n");
}

/* ============================================================================
 * TEST CASES
 * ============================================================================ */

static void test_parse_serialize(void)
{
    struct mcp_message msg;
    char output_buffer[1024];
    size_t output_size;
    parser_status_t status;
    
    printk("\n========== MCP Parser Tests ==========\n");
    
    /* Allow time for header to print */
    k_sleep(K_MSEC(50));
    
    /* Test 1: Parse Request */
    printk("\n[TEST 1] Parse Request\n");
    const char *request = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                         "\"params\":{\"protocolVersion\":\"2024-11-05\"}}";
    
    status = mcp_parse(request, &msg);
    if (status == PARSER_OK) {
        printk("Parse successful\n");
        print_message(&msg);
        
        /* Serialize it back */
        status = mcp_serialize(&msg, output_buffer, sizeof(output_buffer), &output_size);
        if (status == PARSER_OK) {
            printk("Serialize successful\n");
            printk("Output (%zu bytes): %s\n", output_size, output_buffer);
        }
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 2: Parse Response */
    printk("\n[TEST 2] Parse Response\n");
    const char *response = "{\"jsonrpc\":\"2.0\",\"id\":1,"
                          "\"result\":{\"protocolVersion\":\"2024-11-05\","
                          "\"serverInfo\":{\"name\":\"test\",\"version\":\"1.0\"}}}";
    
    status = mcp_parse(response, &msg);
    if (status == PARSER_OK) {
        printk("Parse successful\n");
        print_message(&msg);
        
        /* Serialize it back */
        status = mcp_serialize(&msg, output_buffer, sizeof(output_buffer), &output_size);
        if (status == PARSER_OK) {
            printk("Serialize successful\n");
            printk("Output (%zu bytes): %s\n", output_size, output_buffer);
        }
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 3: Parse Error */
    printk("\n[TEST 3] Parse Error Response\n");
    const char *error = "{\"jsonrpc\":\"2.0\",\"id\":1,"
                       "\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
    
    status = mcp_parse(error, &msg);
    if (status == PARSER_OK) {
        printk("Parse successful\n");
        print_message(&msg);
        
        /* Serialize it back */
        status = mcp_serialize(&msg, output_buffer, sizeof(output_buffer), &output_size);
        if (status == PARSER_OK) {
            printk("Serialize successful\n");
            printk("Output (%zu bytes): %s\n", output_size, output_buffer);
        }
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 4: Parse Notification */
    printk("\n[TEST 4] Parse Notification (no ID)\n");
    const char *notification = "{\"jsonrpc\":\"2.0\",\"method\":\"progress\","
                               "\"params\":{\"progress\":50,\"total\":100}}";
    
    status = mcp_parse(notification, &msg);
    if (status == PARSER_OK) {
        printk("Parse successful\n");
        print_message(&msg);
        
        /* Serialize it back */
        status = mcp_serialize(&msg, output_buffer, sizeof(output_buffer), &output_size);
        if (status == PARSER_OK) {
            printk("Serialize successful\n");
            printk("Output (%zu bytes): %s\n", output_size, output_buffer);
        }
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 5: Build and serialize tools/call with tool name */
    printk("\n[TEST 5] Build and Serialize tools/call Request\n");
    memset(&msg, 0, sizeof(msg));
    msg.msg_class = MCP_MSG_REQUEST;
    msg.id = 42;
    msg.has_id = true;
    strcpy(msg.method, "tools/call");
    strcpy(msg.params, "{\"name\":\"get_time\",\"arguments\":{}}");
    msg.has_params = true;
    strcpy(msg.tool_name, "get_time");
    msg.has_tool_name = true;
    
    status = mcp_serialize(&msg, output_buffer, sizeof(output_buffer), &output_size);
    if (status == PARSER_OK) {
        printk("Serialize successful\n");
        printk("Output (%zu bytes): %s\n", output_size, output_buffer);
        
        /* Parse it back to verify tool name extraction */
        struct mcp_message parsed_msg;
        status = mcp_parse(output_buffer, &parsed_msg);
        if (status == PARSER_OK) {
            printk("Round-trip successful\n");
            print_message(&parsed_msg);
            if (parsed_msg.has_tool_name) {
                printk("Tool name extracted: %s\n", parsed_msg.tool_name);
            }
        }
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 6: Parse tools/call with different tool */
    printk("\n[TEST 6] Parse tools/call with Tool Name\n");
    const char *tool_call = "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"weather_forecast\",\"arguments\":{\"city\":\"NYC\"}}}";
    
    status = mcp_parse(tool_call, &msg);
    if (status == PARSER_OK) {
        printk("✓ Parse successful\n");
        print_message(&msg);
        if (msg.has_tool_name) {
            printk("✓ Successfully extracted tool: %s\n", msg.tool_name);
        }
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 7: Error handling - invalid JSON */
    printk("\n[TEST 7] Error Handling - Invalid JSON\n");
    const char *invalid = "not a json";
    
    status = mcp_parse(invalid, &msg);
    if (status != PARSER_OK) {
        printk("Correctly rejected invalid JSON (error: %d)\n", status);
    }
    
    /* Allow log buffer to drain */
    k_sleep(K_MSEC(50));
    
    /* Test 8: Error handling - missing jsonrpc field */
    printk("\n[TEST 8] Error Handling - Missing jsonrpc\n");
    const char *no_version = "{\"id\":1,\"method\":\"test\"}";
    
    status = mcp_parse(no_version, &msg);
    if (status != PARSER_OK) {
        printk("Correctly rejected message without jsonrpc field (error: %d)\n", status);
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void)
{
    /* Wait for logging system to initialize */
    k_sleep(K_MSEC(100));
    
    printk("\n================================================\n");
    printk("   MCP Stateless JSON Parser\n");
    printk("================================================\n");
    printk("Features:\n");
    printk("Stateless design - no queues or buffers\n");
    printk("Two functions: mcp_parse() and mcp_serialize()\n");
    printk("Works on one message at a time\n");
    printk("Tool name extraction for tools/call\n");
    printk("Transport layer handles all communication\n");
    printk("Basic validation and error handling\n");
    printk("Configurable tool name length: %d chars\n", CONFIG_MCP_TOOL_NAME_MAX_LEN);
    printk("================================================\n");
    
    /* Allow time for intro to print */
    k_sleep(K_MSEC(100));
    
    /* Run tests */
    test_parse_serialize();
    
    /* Allow final logs to print */
    k_sleep(K_MSEC(100));
    
    printk("\n================================================\n");
    printk("   All Tests Complete\n");
    printk("================================================\n");
    
    /* Keep running */
    while (1) {
        k_sleep(K_SECONDS(10));
    }
    
    return 0;
}
