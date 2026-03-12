/*
 * Copyright (c) 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "web_server.h"
#include "rest_api.h"
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/mdns_responder.h>

LOG_MODULE_REGISTER(web_server, LOG_LEVEL_DBG);

#define MDNS_INSTANCE_NAME "zephyr-nxp-rw612-otbr"
#if defined(CONFIG_MDNS_RESPONDER) && defined(CONFIG_DNS_SD)
/* mDNS service configuration */
static const uint16_t http_port = 8080;

static const struct dns_sd_rec otbr_http_service = {
    .instance = MDNS_INSTANCE_NAME,
    .service = "_http",
    .proto = "_tcp",
    .domain = "local",
    .text = DNS_SD_EMPTY_TXT,
    .text_size = 0,
    .port = &http_port,
};
#endif

/* Enhanced HTML content with Thread network configuration form */
static const char index_html[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title>OpenThread Border Router - Configuration</title>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <style>\n"
    "        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }\n"
    "        .container { max-width: 1400px; margin: 0 auto; }\n"
    "        .header { background: #2c3e50; color: white; padding: 20px; text-align: center; border-radius: 8px; margin-bottom: 20px; position: relative; }\n"
    "        .header-content { display: flex; align-items: center; justify-content: center; gap: 20px; }\n"
    "        .header-logo { width: 120px; height: auto; flex-shrink: 0; }\n"
    "        .header-title { flex: 0 1 auto; text-align: center; min-width: 250px; }\n"
    "        .header-title h1 { margin: 0; font-size: 28px; }\n"
    "        .header-title p { margin: 5px 0 0 0; font-size: 16px; }\n"
    "        .grid { display: grid; grid-template-columns: 1fr 1fr 2fr; gap: 20px; }\n"
    "        @media (max-width: 1200px) { .grid { grid-template-columns: 1fr; } }\n"
    "        .card { background: white; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); overflow: hidden; }\n"
    "        .card-header { background: #34495e; color: white; padding: 15px; font-weight: bold; }\n"
    "        .card-content { padding: 20px; }\n"
    "        .form-group { margin-bottom: 15px; }\n"
    "        .form-group label { display: block; margin-bottom: 5px; font-weight: bold; color: #2c3e50; }\n"
    "        .form-group input, .form-group select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n"
    "        .form-group small { color: #7f8c8d; font-size: 12px; }\n"
    "        .button { background: #3498db; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }\n"
    "        .button:hover { background: #2980b9; }\n"
    "        .button.success { background: #27ae60; }\n"
    "        .button.success:hover { background: #229954; }\n"
    "        .button.danger { background: #e74c3c; }\n"
    "        .button.danger:hover { background: #c0392b; }\n"
    "        .button.warning { background: #f39c12; }\n"
    "        .button.warning:hover { background: #d68910; }\n"
    "        pre { background: #f8f9fa; padding: 15px; border-radius: 5px; overflow-x: auto; font-size: 12px; }\n"
    "        .loading { color: #7f8c8d; font-style: italic; }\n"
    "        .error { color: #e74c3c; font-weight: bold; }\n"
    "        .success-msg { color: #27ae60; font-weight: bold; }\n"
    "        .status-online { color: #27ae60; font-weight: bold; }\n"
    "        .status-offline { color: #e74c3c; font-weight: bold; }\n"
    "        .info-box { background: #e8f4f8; border-left: 4px solid #3498db; padding: 10px; margin: 10px 0; border-radius: 4px; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class=\"container\">\n"
    "        <div class=\"header\">\n"
    "            <div class=\"header-content\">\n"
    "                <img src=\"data:image/png;base64,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\" alt=\"NXP Logo\" class=\"header-logo\">\n"
    "                <div class=\"header-title\">\n"
    "                    <h1>OpenThread Border Router</h1>\n"
    "                    <h1>RW612</h1>\n"
    "                    <div id=\"connection-status\" class=\"status-online\">Connected</div>\n"
    "                </div>\n"
    "                <img src=\"data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4gPHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHhtbG5zOnhsaW5rPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3hsaW5rIiB2aWV3Qm94PSIwIDAgNDQwIDIyOCI+PGRlZnM+PHN0eWxlPi5jbHMtMXtmaWxsOiM3OTI5ZDI7fS5jbHMtMntmaWxsOiM5NDU0ZGI7fS5jbHMtM3tmaWxsOiNhZjdmZTQ7fS5jbHMtNHtmaWxsOnVybCgjbGluZWFyLWdyYWRpZW50KTt9LmNscy01e2ZpbGw6dXJsKCNsaW5lYXItZ3JhZGllbnQtMik7fS5jbHMtNntmaWxsOnVybCgjbGluZWFyLWdyYWRpZW50LTMpO30uY2xzLTd7ZmlsbDp1cmwoI2xpbmVhci1ncmFkaWVudC00KTt9LmNscy04e2ZpbGw6I2ZmZjt9PC9zdHlsZT48bGluZWFyR3JhZGllbnQgaWQ9ImxpbmVhci1ncmFkaWVudCIgeDE9IjE3NC4zNzYxNSIgeTE9Ijg0LjI1MTYzIiB4Mj0iMjY4LjI4MTM5IiB5Mj0iODQuMjUxNjMiIGdyYWRpZW50VW5pdHM9InVzZXJTcGFjZU9uVXNlIj48c3RvcCBvZmZzZXQ9IjAiIHN0b3AtY29sb3I9IiM3OTI5ZDIiPjwvc3RvcD48c3RvcCBvZmZzZXQ9IjEiIHN0b3AtY29sb3I9IiMwMDcwYzUiPjwvc3RvcD48L2xpbmVhckdyYWRpZW50PjxsaW5lYXJHcmFkaWVudCBpZD0ibGluZWFyLWdyYWRpZW50LTIiIHgxPSIxODAuNzY3NDEiIHkxPSIxLjUyNDI0IiB4Mj0iMTc5Ljk4MDkiIHkyPSI5NC4zNTAzOSIgZ3JhZGllbnRVbml0cz0idXNlclNwYWNlT25Vc2UiPjxzdG9wIG9mZnNldD0iMCIgc3RvcC1jb2xvcj0iIzAwYWVmZiI+PC9zdG9wPjxzdG9wIG9mZnNldD0iMSIgc3RvcC1jb2xvcj0iIzk0NTRkYiI+PC9zdG9wPjwvbGluZWFyR3JhZGllbnQ+PGxpbmVhckdyYWRpZW50IGlkPSJsaW5lYXItZ3JhZGllbnQtMyIgeDE9IjIzMi40NTU2OCIgeTE9IjQwLjAxNDk0IiB4Mj0iMjEyLjQwNzM3IiB5Mj0iNS4yOTAyNSIgZ3JhZGllbnRVbml0cz0idXNlclNwYWNlT25Vc2UiPjxzdG9wIG9mZnNldD0iMCIgc3RvcC1jb2xvcj0iIzY2YTlkYyI+PC9zdG9wPjxzdG9wIG9mZnNldD0iMSIgc3RvcC1jb2xvcj0iI2IxZTRmYSI+PC9zdG9wPjwvbGluZWFyR3JhZGllbnQ+PGxpbmVhckdyYWRpZW50IGlkPSJsaW5lYXItZ3JhZGllbnQtNCIgeDE9IjIzNi45MTUyMSIgeTE9IjEyLjgwNjczIiB4Mj0iMjQ4LjMwNTk3IiB5Mj0iOTEuNTkyODUiIHhsaW5rOmhyZWY9IiNsaW5lYXItZ3JhZGllbnQtMiI+PC9saW5lYXJHcmFkaWVudD48L2RlZnM+PGcgaWQ9IkNvbG9yZWRfUG9zaXRpdmUiIGRhdGEtbmFtZT0iQ29sb3JlZCBQb3NpdGl2ZSI+PHBvbHlnb24gY2xhc3M9ImNscy0xIiBwb2ludHM9IjEzOS4yMzUgMTIxLjgxNiAxMDguMjEzIDEzNC45MTQgMTM0LjU4OSAxNDguMjM1IDEzOS4yMzUgMTIxLjgxNiI+PC9wb2x5Z29uPjxwb2x5Z29uIGNsYXNzPSJjbHMtMiIgcG9pbnRzPSI3Mi4yNTcgMTM5LjE3NCAzOS4xNjcgMTI2Ljc4NiA0Ni44MzkgMTUyLjgwMSA3Mi4yNTcgMTM5LjE3NCI+PC9wb2x5Z29uPjxwb2x5Z29uIGNsYXNzPSJjbHMtMyIgcG9pbnRzPSI4Ny45NDggMTE1Ljg4OSAxMDguMjEzIDEzNC45MTQgNzIuMjU3IDEzOS4xNzQgODcuOTQ4IDExNS44ODkiPjwvcG9seWdvbj48cG9seWdvbiBjbGFzcz0iY2xzLTMiIHBvaW50cz0iNDMuMTYyIDk4LjAxMyAzOS4xNjcgMTI2Ljc4NiAxNC4xNTUgMTAxLjgyMSA0My4xNjIgOTguMDEzIj48L3BvbHlnb24+PHBvbHlnb24gY2xhc3M9ImNscy00IiBwb2ludHM9IjEzOS4yMzUgMTIxLjgxNSAyMjAuODk4IDQ2LjY4OCAyNTcuNjA5IDk1LjMzMiAxMzkuMjM1IDEyMS44MTUiPjwvcG9seWdvbj48cG9seWdvbiBjbGFzcz0iY2xzLTUiIHBvaW50cz0iMTM5LjIzNSAxMjEuODE1IDE3NC4xMTYgMTEuMjc1IDIyMC44OTggNDYuNjg4IDEzOS4yMzUgMTIxLjgxNSI+PC9wb2x5Z29uPjxwb2x5Z29uIGNsYXNzPSJjbHMtNiIgcG9pbnRzPSIxNzQuMTE2IDExLjI3NSAyNTcuNjA5IDExLjI3NSAyMjAuODk4IDQ2LjY4OCAxNzQuMTE2IDExLjI3NSI+PC9wb2x5Z29uPjxwb2x5Z29uIGNsYXNzPSJjbHMtNyIgcG9pbnRzPSIyNTcuNjA5IDk1LjMzMiAyNTcuNjA5IDExLjI3NSAyMjAuODk4IDQ2LjY4OCAyNTcuNjA5IDk1LjMzMiI+PC9wb2x5Z29uPjxwYXRoIGNsYXNzPSJjbHMtOCIgZD0iTTE3My43Mjg0OSwyMDQuMTYwNzF2LTguODMybDIzLjU2NDQ1LTMyLjk1MzEzSDE3NC44NDE3N3YtOS41NzQyMWgzNC45NTd2OC40MjM4MmwtMjMuNzg3MTEsMzMuMzYxMzNoMjQuNDE4djkuNTc0MjJaIj48L3BhdGg+PHBhdGggY2xhc3M9ImNscy04IiBkPSJNMjI1LjAwNTgzLDE4Ny4yMDE3M3EuMjU5MjgsNS4xOTU3OSwyLjUyMzQ0LDcuNDU5LDIuMjYzMTksMi4yNjM5Miw3LjEyNSwyLjI2MzY3YTE1Ljc5OCwxNS43OTgsMCwwLDAsNC45NTQxLS43MDUwOCw3LjkzNjU4LDcuOTM2NTgsMCwwLDAsMy43Mjk0OS0yLjcwOWw3LjEyNSw1LjA4NGExNC40MTQ4MiwxNC40MTQ4MiwwLDAsMS02LjA2NzM4LDUuMDQ2ODhRMjQwLjY2NiwyMDUuMzExMSwyMzQuMjA5LDIwNS4zMTExcS05LjYxMjMxLDAtMTQuMzQyNzgtNS4wNDY4Ny00LjczMTQ0LTUuMDQ2MzktNC43MzE0NC0xNS4yNTIsMC0yMC44OTIzMywxOC43MDMxMi0yMC44OTI1Nyw4LjMxMTUzLDAsMTIuNjcyODYsNC42OTQzMyw0LjM1OTM4LDQuNjk0ODIsNC4zNjAzNSwxMy42NzQ4MXY0LjcxMjg5Wm0xNi4zNjUyNC02LjYwNTQ3cTAtOC44Njg5LTcuNzU1ODYtOC44NjkxNGE4Ljg2NzA2LDguODY3MDYsMCwwLDAtNC4xOTMzNi44NzIwNyw2LjUxOTU3LDYuNTE5NTcsMCwwLDAtMi42MTYyMSwyLjU5NzY1LDE2LjM4NDA3LDE2LjM4NDA3LDAsMCwwLTEuNTQsNS4zOTk0MloiPjwvcGF0aD48cGF0aCBjbGFzcz0iY2xzLTgiIGQ9Ik0yOTMuOTQ4MjIsMTg0LjQxODUyYTI5LjkzMzQ3LDI5LjkzMzQ3LDAsMCwxLTEuODkyNTgsMTEuMDU4NiwxNS44NjkzNSwxNS44NjkzNSwwLDAsMS01LjQxOCw3LjMxMDU0LDE0LjA5MTU0LDE0LjA5MTU0LDAsMCwxLTguNDIzODMsMi41MjM0NCwxNi4wNDA4OSwxNi4wNDA4OSwwLDAsMS0xMS4wMjE0OC00LjA0NDkydjE3LjU1MjczaC05LjU3NDIyVjE2NS4yMzNoOC44MzJ2My44MjIyNmEyMi45ODI3NywyMi45ODI3NywwLDAsMSw1LjYyMjA3LTMuNjE4MTYsMTYuNDYwNzQsMTYuNDYwNzQsMCwwLDEsNi43NzI0Ni0xLjMxNzM4cTcuMzg0MjgsMCwxMS4yNDQxNCw1LjE5NTMxUTI5My45NDcyNSwxNzQuNTEwODEsMjkzLjk0ODIyLDE4NC40MTg1MlptLTkuODcxMS4xNDg0NHEwLTUuNjc3NzQtMS45NjY4LTguODUwNThhNi40NTQ2Nyw2LjQ1NDY3LDAsMCwwLTUuODYzMjgtMy4xNzI4NiwxMS44MTI0NiwxMS44MTI0NiwwLDAsMC00LjgwNTY2LDEuMDk0NzMsMTYuMDg3ODYsMTYuMDg3ODYsMCwwLDAtNC4yNDksMi42OTA0M3YxNy4wMzMyYTE1LjMwNjkxLDE1LjMwNjkxLDAsMCwwLDMuNzY2NiwyLjQzMDY3LDEwLjcyMzcyLDEwLjcyMzcyLDAsMCwwLDQuNjIwMTEsMS4wNTc2MSw3LjE0MDI3LDcuMTQwMjcsMCwwLDAsNi4yMTU4My0zLjM1ODM5QTE1LjYyMTA1LDE1LjYyMTA1LDAsMCwwLDI4NC4wNzcxMiwxODQuNTY3WiI+PC9wYXRoPjxwYXRoIGNsYXNzPSJjbHMtOCIgZD0iTTMyNS45NjU3OSwyMDQuMTYwNzFWMTgyLjcxMTQ5cTAtNi4xNTk2Ni0xLjE1MDM5LTguMTY0MDZhNC4zMDQ1LDQuMzA0NSwwLDAsMC00LjA4Mi0yLjAwMzkxcS00LjI2ODU1LDAtMTAuMjc5Myw1LjY0MDYzdjI1Ljk3NjU2aC05LjU3NDIxVjE0OC45MDQ4NWg5LjU3NDIxdjIxLjM3NWEyMi4wNDY0OCwyMi4wNDY0OCwwLDAsMSw2LjQ5NDE1LTQuNjAxNTYsMTYuMzM3NjUsMTYuMzM3NjUsMCwwLDEsNi43NTM5LTEuNTU4NTlxNi41MzAyOCwwLDkuMTg0NTcsMy42NzM4MiwyLjY1Mjg0LDMuNjczODMsMi42NTMzMiwxMC4zOTA2M3YyNS45NzY1NloiPjwvcGF0aD48cGF0aCBjbGFzcz0iY2xzLTgiIGQ9Ik0zNDAuMDYwNTIsMjA5LjM5MzEzYTE3LjM4NjQ5LDE3LjM4NjQ5LDAsMCwwLDUuNjAzNTIsMS4wNzYxNyw1LjkwNzU2LDUuOTA3NTYsMCwwLDAsMy43ODUxNS0xLjI5ODgycTEuNjMxODQtMS4yOTkzMSwzLjM3Ny01LjM4MDg2bDEuMDAyLTIuMzAwNzhMMzM5Ljc2MzY1LDE2NS4yMzNIMzQ5LjcwOWw5LjIwMzEyLDI0Ljg2MzI4LDkuNDI1NzgtMjQuODYzMjhoOS43MjI2NmwtMTYuNzM2MzMsNDEuNTk5NjFhMzEuMDA2LDMxLjAwNiwwLDAsMS0zLjk3MDcsNy40MjE4NywxMS4zNjI0NSwxMS4zNjI0NSwwLDAsMS00LjUwODc5LDMuNDg4MjgsMTYuOTA3MiwxNi45MDcyLDAsMCwxLTYuNDc1NTksMS4wNzYxNywzMS44NjYyNSwzMS44NjYyNSwwLDAsMS04LjM0OTYxLTEuMTEzMjhaIj48L3BhdGg+PHBhdGggY2xhc3M9ImNscy04IiBkPSJNNDA1Ljk5NjA3LDE3NC41ODQ1NGE4Ljg1MTM5LDguODUxMzksMCwwLDAtMy45MzM2LS43NDIxOSwxMC41ODY4OSwxMC41ODY4OSwwLDAsMC01LjA2NTQzLDEuMjgwMjgsMjIuMzI0MzYsMjIuMzI0MzYsMCwwLDAtNC45MTcsMy43Mjk0OXYyNS4zMDg1OWgtOS41NzQyMlYxNjUuMjMzaDkuNTc0MjJ2NS41NjY0YTI0Ljk5MywyNC45OTMsMCwwLDEsNS4yNjk1My00LjQxNiwxMS43NTEzMSwxMS43NTEzMSwwLDAsMSw2LjMwODYtMS43NDQxNCw4LjQ3MzIzLDguNDczMjMsMCwwLDEsNC4yMzA0Ny43NzkyOVoiPjwvcGF0aD48cGF0aCBjbGFzcz0iY2xzLTgiIGQ9Ik00MTkuMzA3NDUsMTYyLjgwODExaC0uOTA0MDV2Mi4wNzY4MmgtMS4yMzR2LTUuNzk1NzRoMi43MTE0NmExLjgwNDksMS44MDQ5LDAsMCwxLDEuOTQ2NDMsMS44NjgyOCwxLjY2MzE3LDEuNjYzMTcsMCwwLDEtMS4yNjkyNiwxLjcyMDI1bDEuMzAzODUsMi4yMDcyMWgtMS40MTY2Wm0uMzkwODEtMi42NTg4OUg0MTguNDAzNHYxLjU5ODg2aDEuMjk0ODZhLjgwMjIuODAyMiwwLDEsMCwwLTEuNTk4ODZaIj48L3BhdGg+PHBhdGggY2xhc3M9ImNscy04IiBkPSJNNDE5LjUxNTI2LDE2OC4yODA4MmE2LjI5Mzc2LDYuMjkzNzYsMCwxLDEsNi4yOTQzNS02LjI5Mzk1QTYuMzAwODIsNi4zMDA4MiwwLDAsMSw0MTkuNTE1MjYsMTY4LjI4MDgyWm0wLTExLjc5MTNhNS40OTc1NCw1LjQ5NzU0LDAsMSwwLDUuNDk4MTIsNS40OTczNUE1LjUwMzc0LDUuNTAzNzQsMCwwLDAsNDE5LjUxNTI2LDE1Ni40ODk1MloiPjwvcGF0aD48L2c+PC9zdmc+IA==\" alt=\"Zephyr Logo\" class=\"header-logo\">\n"
    "            </div>\n"
    "        </div>\n"
    "        <div class=\"grid\">\n"
    "            <div class=\"card\">\n"
    "                <div class=\"card-header\">Thread Network Configuration</div>\n"
    "                <div class=\"card-content\">\n"
    "                    <form id=\"network-config-form\" onsubmit=\"submitNetworkConfig(event)\">\n"
    "                        <div class=\"form-group\">\n"
    "                            <label for=\"network_name\">Network Name:</label>\n"
    "                            <input type=\"text\" id=\"network_name\" name=\"network_name\" maxlength=\"16\" placeholder=\"MyThreadNetwork\">\n"
    "                            <small>Max 16 characters</small>\n"
    "                        </div>\n"
    "                        <div class=\"form-group\">\n"
    "                            <label for=\"pan_id\">PAN ID:</label>\n"
    "                            <input type=\"text\" id=\"pan_id\" name=\"pan_id\" placeholder=\"0x1234\">\n"
    "                            <small>Hexadecimal format (e.g., 0x1234)</small>\n"
    "                        </div>\n"
    "                        <div class=\"form-group\">\n"
    "                            <label for=\"extpanid\">Extended PAN ID:</label>\n"
    "                            <input type=\"text\" id=\"extpanid\" name=\"extpanid\" placeholder=\"0011223344556677\" maxlength=\"16\">\n"
    "                            <small>16 hex characters (8 bytes)</small>\n"
    "                            <button type=\"button\" class=\"button warning\" onclick=\"generateRandomExtPanId()\" style=\"margin-top: 5px;\">Generate Random Extended PAN ID</button>\n"
    "                        </div>\n"
    "                        <div class=\"form-group\">\n"
    "                            <label for=\"channel\">Channel:</label>\n"
    "                            <select id=\"channel\" name=\"channel\">\n"
    "                                <option value=\"\">-- Select Channel --</option>\n"
    "                                <option value=\"11\">11 (2405 MHz)</option>\n"
    "                                <option value=\"12\">12 (2410 MHz)</option>\n"
    "                                <option value=\"13\">13 (2415 MHz)</option>\n"
    "                                <option value=\"14\">14 (2420 MHz)</option>\n"
    "                                <option value=\"15\" selected>15 (2425 MHz)</option>\n"
    "                                <option value=\"16\">16 (2430 MHz)</option>\n"
    "                                <option value=\"17\">17 (2435 MHz)</option>\n"
    "                                <option value=\"18\">18 (2440 MHz)</option>\n"
    "                                <option value=\"19\">19 (2445 MHz)</option>\n"
    "                                <option value=\"20\">20 (2450 MHz)</option>\n"
    "                                <option value=\"21\">21 (2455 MHz)</option>\n"
    "                                <option value=\"22\">22 (2460 MHz)</option>\n"
    "                                <option value=\"23\">23 (2465 MHz)</option>\n"
    "                                <option value=\"24\">24 (2470 MHz)</option>\n"
    "                                <option value=\"25\">25 (2475 MHz)</option>\n"
    "                                <option value=\"26\">26 (2480 MHz)</option>\n"
    "                            </select>\n"
    "                            <small>IEEE 802.15.4 channel (11-26)</small>\n"
    "                        </div>\n"
    "                        <div class=\"form-group\">\n"
    "                            <label for=\"network_key\">Network Key:</label>\n"
    "                            <input type=\"text\" id=\"network_key\" name=\"network_key\" placeholder=\"00112233445566778899aabbccddeeff\" maxlength=\"32\">\n"
    "                            <small>32 hex characters (16 bytes)</small>\n"
    "                            <button type=\"button\" class=\"button warning\" onclick=\"generateRandomKey()\" style=\"margin-top: 5px;\">Generate Random Key</button>\n"
    "                        </div>\n"
    "                        <div class=\"info-box\">\n"
    "                            <strong>Info:</strong> A new dataset will be created with random values. Fill in fields to override specific parameters.\n"
    "                        </div>\n"
    "                        <button type=\"submit\" class=\"button success\" style=\"width: 100%;\">Save Configuration</button>\n"
    "                    </form>\n"
    "                    <div id=\"config-result\" style=\"margin-top: 15px;\"></div>\n"
    "                </div>\n"
    "            </div>\n"
    "            <div class=\"card\">\n"
    "                <div class=\"card-header\">Control Panel</div>\n"
    "                <div class=\"card-content\">\n"
    "                    <button class=\"button success\" onclick=\"startThread()\" style=\"width: 100%; margin-bottom: 10px;\">Start Thread Network</button>\n"
    "                    <button class=\"button danger\" onclick=\"stopThread()\" style=\"width: 100%; margin-bottom: 10px;\">Stop Thread Network</button>\n"
    "                    <button class=\"button\" onclick=\"loadCurrentConfig()\" style=\"width: 100%; margin-bottom: 10px;\">Load Current Config</button>\n"
    "                    <button class=\"button\" onclick=\"getDatasetHex()\" style=\"width: 100%; margin-bottom: 10px;\">Get Dataset (Hex)</button>\n"
    "                    <button class=\"button\" onclick=\"refreshAll()\" style=\"width: 100%;\">Refresh Status</button>\n"
    "                    <div id=\"control-result\" style=\"margin-top: 10px;\"></div>\n"
    "                </div>\n"
    "            </div>\n"
    "            <div class=\"card\">\n"
    "                <div class=\"card-header\">Thread Network Topology</div>\n"
    "                <div class=\"card-content\">\n"
    "                    <button class=\"button\" onclick=\"refreshTopology()\" style=\"margin-bottom: 10px;\">Refresh Topology</button>\n"
    "                    <canvas id=\"network-topology\" width=\"800\" height=\"400\" style=\"width: 100%; border: 1px solid #ddd; border-radius: 5px; background: #f9f9f9;\"></canvas>\n"
    "                    <div id=\"topology-info\" style=\"margin-top: 10px;\"></div>\n"
    "                    <div style=\"margin-top: 10px; font-size: 12px;\">\n"
    "                        <span style=\"color: #ffcc00;\">★</span> Leader &nbsp;\n"
    "                        <span style=\"color: #99ff99;\">◆</span> Router &nbsp;\n"
    "                        <span style=\"color: #99ccff;\">●</span> Child &nbsp;\n"
    "                        <span style=\"color: #ff9999;\">■</span> Detached\n"
    "                    </div>\n"
    "                </div>\n"
    "            </div>\n"
    "            <div class=\"card\">\n"
    "                <div class=\"card-header\">Border Router Status</div>\n"
    "                <div class=\"card-content\">\n"
    "                    <div id=\"status-content\" class=\"loading\">Loading status...</div>\n"
    "                </div>\n"
    "            </div>\n"
    "            <div class=\"card\">\n"
    "                <div class=\"card-header\">Current Network Dataset</div>\n"
    "                <div class=\"card-content\">\n"
    "                    <div id=\"dataset-content\" class=\"loading\">Loading dataset...</div>\n"
    "                </div>\n"
    "            </div>\n"
    "        </div>\n"
    "    </div>\n"
    "    <script>\n"
    "        function fetchAPI(url, method = 'GET', body = null) {\n"
    "            const options = { method: method };\n"
    "            if (body) {\n"
    "                options.headers = { 'Content-Type': 'application/x-www-form-urlencoded' };\n"
    "                options.body = body;\n"
    "            }\n"
    "            return fetch(url, options)\n"
    "                .then(response => {\n"
    "                    if (response.headers.get('content-type')?.includes('application/json')) {\n"
    "                        return response.json();\n"
    "                    } else {\n"
    "                        return response.text().then(text => {\n"
    "                            try { return JSON.parse(text); } catch(e) { return {data: text}; }\n"
    "                        });\n"
    "                    }\n"
    "                })\n"
    "                .catch(error => ({ error: error.message }));\n"
    "        }\n"
    "        function refreshStatus() {\n"
    "            fetchAPI('/api/status').then(data => {\n"
    "                document.getElementById('status-content').innerHTML = '<pre>' + JSON.stringify(data, null, 2) + '</pre>';\n"
    "                updateConnectionStatus(true);\n"
    "            }).catch(error => {\n"
    "                document.getElementById('status-content').innerHTML = '<div class=\"error\">Failed to load status</div>';\n"
    "                updateConnectionStatus(false);\n"
    "            });\n"
    "        }\n"
    "        function refreshDataset() {\n"
    "            fetchAPI('/api/dataset').then(data => {\n"
    "                document.getElementById('dataset-content').innerHTML = '<pre>' + JSON.stringify(data, null, 2) + '</pre>';\n"
    "            }).catch(error => {\n"
    "                document.getElementById('dataset-content').innerHTML = '<div class=\"error\">Failed to load dataset</div>';\n"
    "            });\n"
    "        }\n"
    "        function updateConnectionStatus(connected) {\n"
    "            const statusEl = document.getElementById('connection-status');\n"
    "            if (connected) {\n"
    "                statusEl.innerHTML = 'Connected';\n"
    "                statusEl.className = 'status-online';\n"
    "            } else {\n"
    "                statusEl.innerHTML = 'Disconnected';\n"
    "                statusEl.className = 'status-offline';\n"
    "            }\n"
    "        }\n"
    "        function refreshAll() {\n"
    "            refreshStatus();\n"
    "            refreshDataset();\n"
    "            refreshTopology();\n"
    "        }\n"
    "        function startThread() {\n"
    "            fetchAPI('/api/thread/start').then(data => {\n"
    "                document.getElementById('control-result').innerHTML = '<pre>' + JSON.stringify(data, null, 2) + '</pre>';\n"
    "                setTimeout(refreshAll, 1000);\n"
    "            });\n"
    "        }\n"
    "        function stopThread() {\n"
    "            fetchAPI('/api/thread/stop').then(data => {\n"
    "                document.getElementById('control-result').innerHTML = '<pre>' + JSON.stringify(data, null, 2) + '</pre>';\n"
    "                setTimeout(refreshAll, 1000);\n"
    "            });\n"
    "        }\n"
    "        function submitNetworkConfig(event) {\n"
    "            event.preventDefault();\n"
    "            const form = document.getElementById('network-config-form');\n"
    "            const formData = new FormData(form);\n"
    "            const params = new URLSearchParams();\n"
    "            for (const [key, value] of formData) {\n"
    "                if (value) params.append(key, value);\n"
    "            }\n"
    "            document.getElementById('config-result').innerHTML = '<div class=\"loading\">Saving configuration...</div>';\n"
    "            fetchAPI('/api/network/config', 'POST', params.toString()).then(data => {\n"
    "                if (data.error) {\n"
    "                    document.getElementById('config-result').innerHTML = '<div class=\"error\">Error: ' + data.error + '</div>';\n"
    "                } else {\n"
    "                    document.getElementById('config-result').innerHTML = '<div class=\"success-msg\">' + data.message + '</div>';\n"
    "                    setTimeout(refreshDataset, 500);\n"
    "                }\n"
    "            });\n"
    "        }\n"
    "        function loadCurrentConfig() {\n"
    "            fetchAPI('/api/dataset').then(data => {\n"
    "                if (data.network_name) {\n"
    "                    document.getElementById('network_name').value = data.network_name;\n"
    "                }\n"
    "                if (data.pan_id) {\n"
    "                    document.getElementById('pan_id').value = data.pan_id;\n"
    "                }\n"
    "                if (data.extpanid && data.extpanid !== 'null') {\n"
    "                    document.getElementById('extpanid').value = data.extpanid;\n"
    "                }\n"
    "                if (data.channel) {\n"
    "                    document.getElementById('channel').value = data.channel;\n"
    "                }\n"
    "                if (data.network_key && data.network_key !== 'null') {\n"
    "                    document.getElementById('network_key').value = data.network_key;\n"
    "                }\n"
    "                document.getElementById('control-result').innerHTML = '<div class=\"success-msg\">Configuration loaded</div>';\n"
    "            }).catch(error => {\n"
    "                document.getElementById('control-result').innerHTML = '<div class=\"error\">Failed to load configuration</div>';\n"
    "            });\n"
    "        }\n"
    "        function getDatasetHex() {\n"
    "            fetchAPI('/api/dataset/hex').then(data => {\n"
    "                if (data.error) {\n"
    "                    document.getElementById('control-result').innerHTML = '<div class=\"error\">Error: ' + data.error + '</div>';\n"
    "                } else {\n"
    "                    window.currentDatasetHex = data.dataset_hex;\n"
    "                    const hexDisplay = '<div class=\"success-msg\">Dataset retrieved successfully</div>' +\n"
    "                                      '<div style=\"margin-top: 10px;\"><strong>Hex Dataset (' + data.length + ' bytes):</strong></div>' +\n"
    "                                      '<textarea readonly style=\"width: 100%; height: 100px; font-family: monospace; font-size: 12px; margin-top: 5px;\" id=\"dataset-hex-text\">' + data.dataset_hex + '</textarea>' +\n"
    "                                      '<button class=\"button\" onclick=\"copyDatasetToClipboard()\" style=\"margin-top: 5px;\">Copy to Clipboard</button>';\n"
    "                    document.getElementById('control-result').innerHTML = hexDisplay;\n"
    "                }\n"
    "            });\n"
    "        }\n"
    "        function copyDatasetToClipboard() {\n"
    "            const textarea = document.getElementById('dataset-hex-text');\n"
    "            if (!textarea) {\n"
    "                showCopyMessage('Textarea not found', true);\n"
    "                return;\n"
    "            }\n"
    "            const text = textarea.value;\n"
    "            \n"
    "            // Méthode 1: Clipboard API moderne\n"
    "            if (navigator.clipboard && navigator.clipboard.writeText) {\n"
    "                navigator.clipboard.writeText(text).then(() => {\n"
    "                    showCopyMessage('Dataset copied to clipboard!', false);\n"
    "                }).catch(err => {\n"
    "                    console.error('Clipboard API failed:', err);\n"
    "                    fallbackCopy(textarea);\n"
    "                });\n"
    "            } else {\n"
    "                fallbackCopy(textarea);\n"
    "            }\n"
    "        }\n"
    "        function fallbackCopy(textarea) {\n"
    "            try {\n"
    "                textarea.select();\n"
    "                textarea.setSelectionRange(0, 99999);\n"
    "                const successful = document.execCommand('copy');\n"
    "                if (successful) {\n"
    "                    showCopyMessage('Dataset copied to clipboard!', false);\n"
    "                } else {\n"
    "                    showCopyMessage('Copy failed. Please select and copy manually (Ctrl+C).', true);\n"
    "                }\n"
    "            } catch (err) {\n"
    "                console.error('Fallback copy failed:', err);\n"
    "                showCopyMessage('Copy not supported. Text selected - use Ctrl+C to copy.', true);\n"
    "                textarea.select();\n"
    "            }\n"
    "        }\n"
    "        function showCopyMessage(message, isError) {\n"
    "            const msgDiv = document.createElement('div');\n"
    "            msgDiv.className = isError ? 'error' : 'success-msg';\n"
    "            msgDiv.textContent = message;\n"
    "            msgDiv.style.marginTop = '10px';\n"
    "            msgDiv.style.padding = '10px';\n"
    "            msgDiv.style.borderRadius = '5px';\n"
    "            msgDiv.style.backgroundColor = isError ? '#ffe6e6' : '#e6ffe6';\n"
    "            \n"
    "            const resultDiv = document.getElementById('control-result');\n"
    "            const existingMsg = resultDiv.querySelector('.copy-message');\n"
    "            if (existingMsg) existingMsg.remove();\n"
    "            \n"
    "            msgDiv.className += ' copy-message';\n"
    "            resultDiv.appendChild(msgDiv);\n"
    "            \n"
    "            setTimeout(() => msgDiv.remove(), 3000);\n"
    "        }\n"
    "        function generateRandomKey() {\n"
    "            const array = new Uint8Array(16);\n"
    "            crypto.getRandomValues(array);\n"
    "            const hexKey = Array.from(array).map(b => b.toString(16).padStart(2, '0')).join('');\n"
    "            document.getElementById('network_key').value = hexKey;\n"
    "        }\n"
    "        function generateRandomExtPanId() {\n"
    "            const array = new Uint8Array(8);\n"
    "            crypto.getRandomValues(array);\n"
    "            const hexExtPanId = Array.from(array).map(b => b.toString(16).padStart(2, '0')).join('');\n"
    "            document.getElementById('extpanid').value = hexExtPanId;\n"
    "        }\n"
    "        let topologyData = null;\n"
    "        function refreshTopology() {\n"
    "            console.log('Refreshing topology...');\n"
    "            fetchAPI('/api/topology').then(data => {\n"
    "                console.log('Topology data received:', data);\n"
    "                if (data.error) {\n"
    "                    document.getElementById('topology-info').innerHTML = '<div class=\"error\">Error: ' + data.error + '</div>';\n"
    "                } else {\n"
    "                    topologyData = data;\n"
    "                    drawTopology(data);\n"
    "                    document.getElementById('topology-info').innerHTML = '<div class=\"success-msg\">Topology updated: ' + data.nodes.length + ' nodes, ' + data.edges.length + ' connections</div>';\n"
    "                }\n"
    "            }).catch(error => {\n"
    "                console.error('Error fetching topology:', error);\n"
    "                document.getElementById('topology-info').innerHTML = '<div class=\"error\">Failed to load topology</div>';\n"
    "            });\n"
    "        }\n"
    "        function drawTopology(data) {\n"
    "            const canvas = document.getElementById('network-topology');\n"
    "            if (!canvas) return;\n"
    "            const ctx = canvas.getContext('2d');\n"
    "            const width = canvas.width;\n"
    "            const height = canvas.height;\n"
    "            \n"
    "            ctx.clearRect(0, 0, width, height);\n"
    "            \n"
    "            if (!data.nodes || data.nodes.length === 0) {\n"
    "                ctx.fillStyle = '#999';\n"
    "                ctx.font = '16px Arial';\n"
    "                ctx.textAlign = 'center';\n"
    "                ctx.fillText('No nodes in network', width/2, height/2);\n"
    "                return;\n"
    "            }\n"
    "            \n"
    "            const centerX = width / 2;\n"
    "            const centerY = height / 2;\n"
    "            const radius = Math.min(width, height) * 0.35;\n"
    "            \n"
    "            const positions = {};\n"
    "            data.nodes.forEach((node, index) => {\n"
    "                if (node.shape === 'star') {\n"
    "                    positions[node.id] = { x: centerX, y: centerY };\n"
    "                } else {\n"
    "                    const angle = (2 * Math.PI * index) / (data.nodes.length - 1);\n"
    "                    positions[node.id] = {\n"
    "                        x: centerX + radius * Math.cos(angle),\n"
    "                        y: centerY + radius * Math.sin(angle)\n"
    "                    };\n"
    "                }\n"
    "            });\n"
    "            \n"
    "            ctx.strokeStyle = '#666';\n"
    "            ctx.lineWidth = 2;\n"
    "            data.edges.forEach(edge => {\n"
    "                const from = positions[edge.from];\n"
    "                const to = positions[edge.to];\n"
    "                if (from && to) {\n"
    "                    ctx.beginPath();\n"
    "                    ctx.moveTo(from.x, from.y);\n"
    "                    ctx.lineTo(to.x, to.y);\n"
    "                    ctx.stroke();\n"
    "                    \n"
    "                    const angle = Math.atan2(to.y - from.y, to.x - from.x);\n"
    "                    const arrowLen = 10;\n"
    "                    ctx.beginPath();\n"
    "                    ctx.moveTo(to.x, to.y);\n"
    "                    ctx.lineTo(to.x - arrowLen * Math.cos(angle - Math.PI/6), to.y - arrowLen * Math.sin(angle - Math.PI/6));\n"
    "                    ctx.moveTo(to.x, to.y);\n"
    "                    ctx.lineTo(to.x - arrowLen * Math.cos(angle + Math.PI/6), to.y - arrowLen * Math.sin(angle + Math.PI/6));\n"
    "                    ctx.stroke();\n"
    "                    \n"
    "                    if (edge.label) {\n"
    "                        const midX = (from.x + to.x) / 2;\n"
    "                        const midY = (from.y + to.y) / 2;\n"
    "                        ctx.fillStyle = '#fff';\n"
    "                        ctx.fillRect(midX - 15, midY - 8, 30, 16);\n"
    "                        ctx.fillStyle = '#333';\n"
    "                        ctx.font = '10px Arial';\n"
    "                        ctx.textAlign = 'center';\n"
    "                        ctx.fillText(edge.label, midX, midY + 4);\n"
    "                    }\n"
    "                }\n"
    "            });\n"
    "            \n"
    "            data.nodes.forEach(node => {\n"
    "                const pos = positions[node.id];\n"
    "                if (!pos) return;\n"
    "                \n"
    "                ctx.fillStyle = node.color;\n"
    "                ctx.strokeStyle = '#333';\n"
    "                ctx.lineWidth = 2;\n"
    "                \n"
    "                if (node.shape === 'star') {\n"
    "                    drawStar(ctx, pos.x, pos.y, 5, 12, 6);\n"
    "                } else if (node.shape === 'diamond') {\n"
    "                    drawDiamond(ctx, pos.x, pos.y, 8);\n"
    "                } else if (node.shape === 'dot') {\n"
    "                    ctx.beginPath();\n"
    "                    ctx.arc(pos.x, pos.y, 5, 0, 2 * Math.PI);\n"
    "                    ctx.fill();\n"
    "                    ctx.stroke();\n"
    "                } else {\n"
    "                    ctx.fillRect(pos.x - 6, pos.y - 6, 12, 12);\n"
    "                    ctx.strokeRect(pos.x - 6, pos.y - 6, 12, 12);\n"
    "                }\n"
    "                \n"
    "                ctx.fillStyle = '#000';\n"
    "                ctx.font = '11px Arial';\n"
    "                ctx.textAlign = 'center';\n"
    "                const lines = node.label.split('\\\\n');\n"
    "                lines.forEach((line, i) => {\n"
    "                    ctx.fillText(line, pos.x, pos.y + 20 + i * 12);\n"
    "                });\n"
    "            });\n"
    "        }\n"
    "        function drawStar(ctx, cx, cy, spikes, outerRadius, innerRadius) {\n"
    "            let rot = Math.PI / 2 * 3;\n"
    "            let x = cx;\n"
    "            let y = cy;\n"
    "            const step = Math.PI / spikes;\n"
    "            ctx.beginPath();\n"
    "            ctx.moveTo(cx, cy - outerRadius);\n"
    "            for (let i = 0; i < spikes; i++) {\n"
    "                x = cx + Math.cos(rot) * outerRadius;\n"
    "                y = cy + Math.sin(rot) * outerRadius;\n"
    "                ctx.lineTo(x, y);\n"
    "                rot += step;\n"
    "                x = cx + Math.cos(rot) * innerRadius;\n"
    "                y = cy + Math.sin(rot) * innerRadius;\n"
    "                ctx.lineTo(x, y);\n"
    "                rot += step;\n"
    "            }\n"
    "            ctx.lineTo(cx, cy - outerRadius);\n"
    "            ctx.closePath();\n"
    "            ctx.fill();\n"
    "            ctx.stroke();\n"
    "        }\n"
    "        function drawDiamond(ctx, cx, cy, size) {\n"
    "            ctx.beginPath();\n"
    "            ctx.moveTo(cx, cy - size);\n"
    "            ctx.lineTo(cx + size, cy);\n"
    "            ctx.lineTo(cx, cy + size);\n"
    "            ctx.lineTo(cx - size, cy);\n"
    "            ctx.closePath();\n"
    "            ctx.fill();\n"
    "            ctx.stroke();\n"
    "        }\n"
    "        setInterval(refreshStatus, 10000);\n"
    "        refreshStatus();\n"
    "        refreshDataset();\n"
    "        refreshTopology();\n"
    "    </script>\n"
    "</body>\n"
    "</html>";

/* Static resource detail for index page */
static struct http_resource_detail_static index_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_STATIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        .content_type = "text/html",
    },
    .static_data = index_html,
    .static_data_len = sizeof(index_html) - 1,
};

/* Dynamic resource details for APIs */
static struct http_resource_detail_dynamic api_status_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = rest_api_status_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_dataset_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = rest_api_dataset_get_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_config_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = rest_api_network_config_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_start_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = rest_api_thread_start_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_stop_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = rest_api_thread_stop_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_dataset_hex_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = rest_api_dataset_hex_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_topology_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = rest_api_topology_handler,
    .user_data = NULL,
};

/* web server state */
static bool web_server_started = false;

/* Define HTTP service */
static uint16_t border_router_service_port = 8080;
HTTP_SERVICE_DEFINE(border_router_service, NULL, &border_router_service_port,
                    CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

/* Define HTTP resources */
HTTP_RESOURCE_DEFINE(index_resource, border_router_service, "/", &index_resource_detail);
HTTP_RESOURCE_DEFINE(api_status_resource, border_router_service, "/api/status", &api_status_detail);
HTTP_RESOURCE_DEFINE(api_dataset_resource, border_router_service, "/api/dataset", &api_dataset_detail);
HTTP_RESOURCE_DEFINE(api_config_resource, border_router_service, "/api/network/config", &api_config_detail);
HTTP_RESOURCE_DEFINE(api_start_resource, border_router_service, "/api/thread/start", &api_start_detail);
HTTP_RESOURCE_DEFINE(api_stop_resource, border_router_service, "/api/thread/stop", &api_stop_detail);
HTTP_RESOURCE_DEFINE(api_dataset_hex_resource, border_router_service, "/api/dataset/hex", &api_dataset_hex_detail);
HTTP_RESOURCE_DEFINE(api_topology_resource, border_router_service, "/api/topology", &api_topology_detail);

#if defined(CONFIG_MDNS_RESPONDER) && defined(CONFIG_DNS_SD)
static void mdns_register_delayed(struct k_work *work)
{
    int ret;
    
    /* Wait for socket service to be ready */
    k_sleep(K_SECONDS(5));
    
    ret = mdns_responder_set_ext_records(&otbr_http_service, 1);
    if (ret < 0) {
        LOG_WRN("Failed to register mDNS service: %d", ret);
    } else {
        LOG_INF("mDNS service registered: %s._http._tcp.local", 
                otbr_http_service.instance);
        LOG_INF("Discoverable at: http://%s.local:%d/", 
                otbr_http_service.instance, border_router_service_port);
    }
}

K_WORK_DEFINE(mdns_work, mdns_register_delayed);
#endif

int web_server_init(void)
{
    int ret;

    if (!web_server_started)
    {
        LOG_INF("Initializing web server on port %d", border_router_service_port);

        /* Initialize REST API */
        ret = rest_api_init();
        if (ret < 0) {
            LOG_ERR("Failed to initialize REST API: %d", ret);
            return ret;
        }

        /* Start the HTTP server */
        ret = http_server_start();
        if (ret < 0) {
            LOG_ERR("Failed to start HTTP service: %d", ret);
            return ret;
        }

#if defined(CONFIG_MDNS_RESPONDER) && defined(CONFIG_DNS_SD)
        /* Schedule mDNS registration for later */
        k_work_submit(&mdns_work);
#endif

        web_server_started = true;
        LOG_INF("HTTP server started successfully");
        
#if defined(CONFIG_MDNS_RESPONDER) && defined(CONFIG_DNS_SD)
        LOG_INF("mDNS registration scheduled...");
#endif
        
        LOG_INF("REST API endpoints:");
        LOG_INF("  GET  /api/status              - Border router status");
        LOG_INF("  GET  /api/dataset             - Get current network dataset");
        LOG_INF("  POST /api/network/config      - Configure Thread network");
        LOG_INF("  GET  /api/thread/start        - Start Thread network");
        LOG_INF("  GET  /api/thread/stop         - Stop Thread network");
        LOG_INF("  GET  /api/dataset/hex         - Get dataset in hex format\");\n");
        LOG_INF("  GET  /api/topology            - Get network topology\");\n");
    }
    else {
        LOG_INF("Web Server already started");
    }

    return 0;
}
