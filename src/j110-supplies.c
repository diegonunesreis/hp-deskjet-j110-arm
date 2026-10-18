#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "hpmud.h"

#define PROBE_CAPACITY 8192
#define RESPONSE_CAPACITY (64 * 1024)
#define J110_URI_PREFIX "hp:/usb/Deskjet_1000_J110_series?"

static char *find_j110_uri(char *probe_output)
{
    char *line = probe_output;
    const size_t prefix_len = sizeof(J110_URI_PREFIX) - 1;
    while (line && *line) {
        char *next = strchr(line, '\n');
        char *uri = strstr(line, "hp:/usb/");
        if (uri && strncasecmp(uri, J110_URI_PREFIX, prefix_len) == 0) {
            char *end = strpbrk(uri, " \r\n");
            if (end) *end = '\0';
            return uri;
        }
        if (!next) break;
        line = next + 1;
    }
    return NULL;
}

static int contains_ci(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = haystack; p && *p; p++)
        if (strncasecmp(p, needle, n) == 0) return 1;
    return 0;
}

static xmlNodePtr find_descendant(xmlNodePtr node, const char *name)
{
    for (xmlNodePtr cur = node; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && xmlStrEqual(cur->name, BAD_CAST name))
            return cur;
        xmlNodePtr found = find_descendant(cur->children, name);
        if (found) return found;
    }
    return NULL;
}

static char *child_text(xmlNodePtr node, const char *name)
{
    xmlNodePtr found = find_descendant(node->children, name);
    if (!found) return NULL;
    xmlChar *content = xmlNodeGetContent(found);
    return (char *)content;
}

static void print_safe(const char *s)
{
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        putchar((isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '/' || c == '.') ? c : '?');
    }
}

static int print_supplies(xmlNodePtr node, int *count)
{
    for (xmlNodePtr cur = node; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && xmlStrEqual(cur->name, BAD_CAST "ConsumableInfo")) {
            char *type = child_text(cur, "ConsumableTypeEnum");
            char *state = child_text(cur, "ConsumableState");
            char *label = child_text(cur, "ConsumableLabelCode");
            char *quantity = child_text(cur, "MeasuredQuantityState");
            char *level = child_text(cur, "ConsumablePercentageLevelRemaining");
            char *end = NULL;
            long pct = level ? strtol(level, &end, 10) : -1;

            int valid_level = level && end != level && *end == '\0' && pct >= 0 && pct <= 100;
            const char *shown_state = state ? state : "unknown";
            int level_is_reliable = valid_level && state && strcmp(state, "missing") != 0;
            if (valid_level && pct == 0 && quantity && strcmp(quantity, "unknown") == 0) {
                level_is_reliable = 0;
            } else if (valid_level && pct == 0 && state && strcmp(state, "missing") != 0) {
                shown_state = "empty";
            } else if (valid_level && pct <= 10 && state && strcmp(state, "missing") != 0) {
                shown_state = "low";
            }

            printf("Consumable %d: type=", ++*count); print_safe(type ? type : "unknown");
            printf(" label="); print_safe(label ? label : "unknown");
            printf(" state="); print_safe(shown_state);
            if (strcmp(shown_state, state ? state : "unknown") != 0) {
                printf(" reported_state="); print_safe(state ? state : "unknown");
            }
            if (level_is_reliable) printf(" remaining=%ld%%\n", pct);
            else if (valid_level && quantity && strcmp(quantity, "unknown") == 0)
                printf(" remaining=unknown (device_level=%ld%%, measurement=unknown)\n", pct);
            else puts(" remaining=unknown");

            if (type) xmlFree(type);
            if (state) xmlFree(state);
            if (label) xmlFree(label);
            if (quantity) xmlFree(quantity);
            if (level) xmlFree(level);
        } else {
            print_supplies(cur->children, count);
        }
    }
    return 0;
}

static int decode_chunked(const char *input, size_t input_len, char *output, size_t output_cap, size_t *output_len)
{
    const char *p = input, *end = input + input_len;
    *output_len = 0;
    while (p < end) {
        const char *line_end = NULL;
        for (const char *q = p; q + 1 < end; q++)
            if (q[0] == '\r' && q[1] == '\n') { line_end = q; break; }
        if (!line_end || line_end - p >= 32) return 0;
        char size_text[32]; size_t size_len = (size_t)(line_end - p);
        memcpy(size_text, p, size_len); size_text[size_len] = '\0';
        char *extension = strchr(size_text, ';'); if (extension) *extension = '\0';
        char *tail = NULL; unsigned long chunk_size = strtoul(size_text, &tail, 16);
        if (tail == size_text || *tail || chunk_size > output_cap - *output_len) return 0;
        p = line_end + 2;
        if (chunk_size == 0) return 1;
        if ((size_t)(end - p) < chunk_size + 2 || p[chunk_size] != '\r' || p[chunk_size + 1] != '\n') return 0;
        memcpy(output + *output_len, p, chunk_size);
        *output_len += chunk_size;
        p += chunk_size + 2;
    }
    return 0;
}

int main(void)
{
    char discovered[PROBE_CAPACITY], uri[512], response[RESPONSE_CAPACITY + 1];
    char body[RESPONSE_CAPACITY + 1];
    int count = 0, discovered_bytes = 0, device = 0, channel = 0;
    int written = 0, response_len = 0, parsed_count = 0;
    enum HPMUD_RESULT result;

    result = hpmud_probe_devices(HPMUD_BUS_USB, discovered, sizeof(discovered), &count, &discovered_bytes);
    char *found_uri = result == HPMUD_R_OK ? find_j110_uri(discovered) : NULL;
    if (!found_uri) { fputs("No supported Deskjet 1000 J110 was found on USB.\n", stderr); return 1; }
    size_t uri_len = strlen(found_uri);
    if (uri_len >= sizeof(uri)) { fputs("J110 USB URI is too long.\n", stderr); return 2; }
    memcpy(uri, found_uri, uri_len + 1);

    result = hpmud_open_device(uri, HPMUD_RAW_MODE, &device);
    if (result != HPMUD_R_OK) { fprintf(stderr, "Could not open J110 USB transport (HPLIP result %d).\n", result); return 3; }
    result = hpmud_open_channel(device, HPMUD_S_EWS_LEDM_CHANNEL, &channel);
    if (result != HPMUD_R_OK) {
        fprintf(stderr, "Could not open the J110 LEDM status channel (HPLIP result %d).\n", result);
        hpmud_close_device(device); return 4;
    }

    const char request[] = "GET /DevMgmt/ConsumableConfigDyn.xml HTTP/1.1\r\nAccept: text/plain\r\nHost:localhost\r\nUser-Agent:hplip\r\n\r\n";
    while (written < (int)sizeof(request) - 1) {
        int part = 0;
        result = hpmud_write_channel(device, channel, request + written,
                                     (int)sizeof(request) - 1 - written, 6, &part);
        if (result != HPMUD_R_OK || part <= 0) break;
        written += part;
    }
    if (written != (int)sizeof(request) - 1) {
        fputs("Could not send LEDM status request.\n", stderr);
        hpmud_close_channel(device, channel); hpmud_close_device(device); return 5;
    }

    while (response_len < RESPONSE_CAPACITY) {
        int got = 0;
        result = hpmud_read_channel(device, channel, response + response_len,
                                    RESPONSE_CAPACITY - response_len, 6, &got);
        if (result == HPMUD_R_IO_TIMEOUT || (result == HPMUD_R_OK && got == 0)) break;
        if (result != HPMUD_R_OK || got < 0) break;
        response_len += got;
    }
    hpmud_close_channel(device, channel);
    hpmud_close_device(device);
    response[response_len] = '\0';

    if (response_len <= 0) { fputs("No response from the J110 LEDM status channel.\n", stderr); return 6; }
    if (!strstr(response, " 200 ")) { fputs("The J110 did not return HTTP 200 for the consumables request.\n", stderr); return 7; }
    char *body_start = strstr(response, "\r\n\r\n");
    if (!body_start) { fputs("Malformed LEDM HTTP response.\n", stderr); return 8; }
    body_start += 4;
    size_t body_len = (size_t)(response + response_len - body_start);
    if (contains_ci(response, "Transfer-Encoding: chunked")) {
        if (!decode_chunked(body_start, body_len, body, RESPONSE_CAPACITY, &body_len)) {
            fputs("Malformed or oversized chunked LEDM response.\n", stderr); return 9;
        }
        body[body_len] = '\0';
    } else {
        if (body_len > RESPONSE_CAPACITY) { fputs("LEDM XML response is too large.\n", stderr); return 10; }
        memcpy(body, body_start, body_len); body[body_len] = '\0';
    }

    xmlDocPtr doc = xmlReadMemory(body, (int)body_len, "consumables.xml", NULL,
                                  XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) { fputs("Could not parse the J110 consumables XML.\n", stderr); return 11; }
    print_supplies(xmlDocGetRootElement(doc), &parsed_count);
    xmlFreeDoc(doc);
    if (!parsed_count) { fputs("The J110 response contained no consumable records.\n", stderr); return 12; }
    return 0;
}
