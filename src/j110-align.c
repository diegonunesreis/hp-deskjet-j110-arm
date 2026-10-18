#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "hpmud.h"

#define DISCOVERY_CAPACITY 8192
#define RESPONSE_CAPACITY 32768
#define URI_PREFIX "hp:/usb/Deskjet_1000_J110_series?"
#define CAL_NS "http://www.hp.com/schemas/imaging/con/cnx/markingagentcalibration/2009/04/08"

static char *find_j110_uri(char *output)
{
    char *line = output;
    while (line && *line) {
        char *next = strchr(line, '\n');
        char *uri = strstr(line, "hp:/usb/");
        if (uri && strncasecmp(uri, URI_PREFIX, sizeof(URI_PREFIX) - 1) == 0) {
            char *end = strpbrk(uri, " \r\n");
            if (end) *end = '\0';
            return uri;
        }
        if (!next) break;
        line = next + 1;
    }
    return NULL;
}

static int write_all(HPMUD_DEVICE device, HPMUD_CHANNEL channel,
                     const char *data, int size)
{
    int offset = 0;
    while (offset < size) {
        int sent = 0;
        enum HPMUD_RESULT r = hpmud_write_channel(device, channel, data + offset,
                                                   size - offset, 6, &sent);
        if (r != HPMUD_R_OK || sent <= 0) return 0;
        offset += sent;
    }
    return 1;
}

static int ledm_request(const char *uri, const char *method, const char *path,
                        const char *body, char *response, size_t response_size)
{
    char request[RESPONSE_CAPACITY];
    size_t body_len = body ? strlen(body) : 0;
    int request_len = snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\nHost:localhost\r\nUser-Agent:hplip\r\n"
        "Accept:text/plain\r\n%s%s%s%sContent-Length:%zu\r\n\r\n%s",
        method, path,
        body ? "Content-Type:text/xml\r\n" : "",
        "", "", "", body_len, body ? body : "");
    if (request_len < 0 || (size_t)request_len >= sizeof(request)) return 0;

    int device = 0, channel = 0;
    enum HPMUD_RESULT r = hpmud_open_device(uri, HPMUD_RAW_MODE, &device);
    if (r != HPMUD_R_OK) return 0;
    r = hpmud_open_channel(device, HPMUD_S_EWS_LEDM_CHANNEL, &channel);
    if (r != HPMUD_R_OK) { hpmud_close_device(device); return 0; }

    int ok = write_all(device, channel, request, request_len);
    size_t total = 0;
    while (ok && total + 1 < response_size) {
        int got = 0;
        r = hpmud_read_channel(device, channel, response + total,
                               (int)(response_size - total - 1), 6, &got);
        if (r == HPMUD_R_IO_TIMEOUT || (r == HPMUD_R_OK && got == 0)) break;
        if (r != HPMUD_R_OK || got < 0) { ok = 0; break; }
        total += (size_t)got;
    }
    hpmud_close_channel(device, channel);
    hpmud_close_device(device);
    response[total] = '\0';
    return ok && total > 0;
}

static int http_status(const char *response)
{
    int status = 0;
    if (sscanf(response, "HTTP/%*s %d", &status) != 1) return 0;
    return status;
}

static const char *http_body(const char *response)
{
    const char *body = strstr(response, "\r\n\r\n");
    return body ? body + 4 : NULL;
}

static xmlNodePtr find_element(xmlNodePtr node, const char *name)
{
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrEqual(current->name, BAD_CAST name))
            return current;
        xmlNodePtr found = find_element(current->children, name);
        if (found) return found;
    }
    return NULL;
}

static int read_calibration_state(const char *uri, char *state, size_t state_size)
{
    char response[RESPONSE_CAPACITY];
    if (!ledm_request(uri, "GET", "/Calibration/State", NULL, response, sizeof(response)) ||
        http_status(response) != 200) return 0;
    const char *body = http_body(response);
    if (!body) return 0;
    xmlDocPtr doc = xmlReadMemory(body, (int)strlen(body), "calibration-state.xml", NULL,
                                  XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return 0;
    xmlChar *content = xmlNodeGetContent(xmlDocGetRootElement(doc));
    snprintf(state, state_size, "%s", content ? (const char *)content : "unknown");
    if (content) xmlFree(content);
    xmlFreeDoc(doc);

    return 1;
}

static int read_alignment_state(const char *uri, char *state, size_t state_size,
                                char *mode, size_t mode_size)
{
    char response[RESPONSE_CAPACITY];
    const char *body;
    xmlDocPtr doc;
    xmlChar *content;
    if (!read_calibration_state(uri, state, state_size)) return 0;

    if (!ledm_request(uri, "GET", "/DevMgmt/ConsumableConfigDyn.xml", NULL,
                      response, sizeof(response)) || http_status(response) != 200) return 0;
    body = http_body(response);
    if (!body) return 0;
    doc = xmlReadMemory(body, (int)strlen(body), "consumables.xml", NULL,
                        XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return 0;
    xmlNodePtr alignment = find_element(xmlDocGetRootElement(doc), "AlignmentMode");
    content = alignment ? xmlNodeGetContent(alignment) : NULL;
    snprintf(mode, mode_size, "%s", content ? (const char *)content : "unsupported");
    if (content) xmlFree(content);
    xmlFreeDoc(doc);
    return 1;
}

static int prompt_choice(const char *label, int minimum, int maximum, int *value)
{
    char line[64], *end = NULL;
    long parsed;
    for (;;) {
        printf("Escolha %s (%d–%d): ", label, minimum, maximum);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) return 0;
        parsed = strtol(line, &end, 10);
        while (end && isspace((unsigned char)*end)) end++;
        if (end != line && end && *end == '\0' && parsed >= minimum && parsed <= maximum) {
            *value = (int)parsed;
            return 1;
        }
        fprintf(stderr, "Informe um número entre %d e %d.\n", minimum, maximum);
    }
}

static int extract_session_path(const char *response, char *path, size_t path_size)
{
    const char *start = strstr(response, "/Calibration/Session/");
    if (start) {
        const char *end = strpbrk(start, "\r\n \"<");
        size_t len = end ? (size_t)(end - start) : strlen(start);
        const char suffix[] = "/ManualSelectedPatterns.xml";
        if (len + sizeof(suffix) > path_size) return 0;
        memcpy(path, start, len);
        path[len] = '\0';
        if (len < sizeof(suffix) - 1 || strcmp(path + len - (sizeof(suffix) - 1), suffix) != 0)
            strncat(path, suffix, path_size - strlen(path) - 1);
        return 1;
    }

    start = strstr(response, "Session/");
    if (!start) return 0;
    start += strlen("Session/");
    const char *end = strpbrk(start, "\r\n \"<");
    size_t len = end ? (size_t)(end - start) : strlen(start);
    const char prefix[] = "/Calibration/Session/";
    const char suffix[] = "/ManualSelectedPatterns.xml";
    if (!len || sizeof(prefix) - 1 + len + sizeof(suffix) > path_size) return 0;
    strcpy(path, prefix);
    strncat(path, start, len);
    strcat(path, suffix);
    return 1;
}

static int choose_alignment_values(int values[10])
{
    static const char *labels[] = {"A (K/C, vertical)", "B (K/C, horizontal)",
        "C (K, vertical)", "D (C, vertical)", "E (K, horizontal)",
        "F (K, horizontal)", "G (K, horizontal)", "H (K, horizontal)",
        "I (K, vertical)", "J (K, vertical)"};
    static const int mins[] = {3,3,3,3,3,3,3,3,3,3};
    static const int maxs[] = {23,17,23,23,11,11,11,11,19,19};
    for (int i = 0; i < 10; i++)
        if (!prompt_choice(labels[i], mins[i], maxs[i], &values[i])) return 0;
    return 1;
}

int main(void)
{
    char discovered[DISCOVERY_CAPACITY], response[RESPONSE_CAPACITY];
    char uri[512], state[128], mode[64], confirm[64], path[1024];
    int count = 0, bytes_read = 0, values[10];
    enum HPMUD_RESULT r = hpmud_probe_devices(HPMUD_BUS_USB, discovered,
                                              sizeof(discovered), &count, &bytes_read);
    char *found = r == HPMUD_R_OK ? find_j110_uri(discovered) : NULL;
    if (!found) { fputs("No supported Deskjet 1000 J110 was found on USB.\n", stderr); return 1; }
    snprintf(uri, sizeof(uri), "%s", found);

    if (!read_alignment_state(uri, state, sizeof(state), mode, sizeof(mode))) {
        fputs("Could not read J110 calibration capability over LEDM.\n", stderr); return 2;
    }
    printf("Calibration state: %s\nAlignment mode: %s\n", state, mode);
    if (strcmp(mode, "manual") != 0) {
        fputs("The printer does not advertise manual alignment.\n", stderr); return 3;
    }
    if (strstr(state, "CalibrationValid")) {
        fputs("The printer reports calibration valid; no alignment page was started.\n", stderr); return 0;
    }
    if (strstr(state, "ParmsRequested")) {
        fputs("The printer is already waiting for alignment choices; restart it before starting a new session.\n", stderr); return 4;
    }
    if (strcmp(state, "CalibrationRequired") != 0) {
        fputs("The printer is in an unrecognized calibration state; no session was started.\n", stderr); return 4;
    }

    fputs("This prints an alignment page and later changes saved calibration. Load plain A4 paper.\n"
          "Type ALIGN J110 to start: ", stdout);
    fflush(stdout);
    if (!fgets(confirm, sizeof(confirm), stdin) || strcmp(confirm, "ALIGN J110\n") != 0) {
        fputs("Alignment canceled.\n", stderr); return 0;
    }

    static const char start_body[] =
        "<cal:CalibrationState xmlns:cal=\"" CAL_NS "\" "
        "xmlns:dd=\"http://www.hp.com/schemas/imaging/con/dictionaries/1.0/\">"
        "Printing</cal:CalibrationState>";
    if (!ledm_request(uri, "POST", "/Calibration/Session", start_body,
                      response, sizeof(response))) {
        fputs("Could not start the calibration session.\n", stderr); return 5;
    }
    int status = http_status(response);
    if (status < 200 || status >= 300) {
        fprintf(stderr, "Printer rejected calibration start (HTTP %d).\n", status); return 6;
    }
    if (!strstr(response, "/Jobs/")) {
        fprintf(stderr, "Calibration page request returned HTTP %d, but no job path was supplied.\n", status);
        return 7;
    }
    char job_path[1024];
    const char *job = strstr(response, "/Jobs/");
    const char *job_end = strpbrk(job, "\r\n \"<");
    size_t job_len = job_end ? (size_t)(job_end - job) : strlen(job);
    if (!job_len || job_len >= sizeof(job_path)) { fputs("Invalid calibration job path.\n", stderr); return 8; }
    memcpy(job_path, job, job_len); job_path[job_len] = '\0';
    int job_ready = 0;
    for (int attempt = 0; attempt < 30; attempt++) {
        if (ledm_request(uri, "GET", job_path, NULL, response, sizeof(response)) &&
            http_status(response) == 200 && extract_session_path(response, path, sizeof(path))) {
            job_ready = 1;
            break;
        }
        sleep(2);
    }
    if (!job_ready) {
        fputs("The calibration page job started, but its selection session path could not be read.\n"
              "Do not start a second alignment session; restart the printer before retrying.\n", stderr);
        return 9;
    }

    puts("A folha de alinhamento foi solicitada. Aguardando a impressora terminar a calibração...");
    int choices_requested = 0;
    for (int attempt = 0; attempt < 60; attempt++) {
        if (!read_calibration_state(uri, state, sizeof(state))) {
            sleep(2);
            continue;
        }
        if (strstr(state, "ParmsRequested")) { choices_requested = 1; break; }
        if (strstr(state, "CalibrationValid")) {
            puts("A impressora concluiu a calibração sem solicitar escolhas manuais.");
            return 0;
        }
        sleep(2);
    }
    if (!choices_requested) {
        fputs("Timeout aguardando a página de alinhamento; nenhuma escolha foi enviada. "
              "Verifique a impressora antes de iniciar outra sessão.\n", stderr);
        return 10;
    }
    puts("Leia agora os números dos padrões A–J na folha.");
    if (!choose_alignment_values(values)) { fputs("Selections canceled; calibration remains pending.\n", stderr); return 11; }

    char xml[4096];
    int xml_len = snprintf(xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<ManualSelectedPatterns xmlns=\"" CAL_NS "\">"
        "<SelectedPattern><Identifier><Id>1</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>2</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>3</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>4</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>5</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>6</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>7</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>8</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>9</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "<SelectedPattern><Identifier><Id>10</Id></Identifier><Choice><Identifier><Id>%d</Id></Identifier></Choice></SelectedPattern>"
        "</ManualSelectedPatterns>", values[0],values[1],values[2],values[3],values[4],
        values[5],values[6],values[7],values[8],values[9]);
    if (xml_len < 0 || (size_t)xml_len >= sizeof(xml)) { fputs("Could not encode selections.\n", stderr); return 12; }
    if (!ledm_request(uri, "PUT", path, xml, response, sizeof(response))) {
        fputs("Could not submit the alignment selections.\n", stderr); return 13;
    }
    status = http_status(response);
    if (status < 200 || status >= 300) {
        fprintf(stderr, "Printer rejected the alignment selections (HTTP %d).\n", status); return 14;
    }
    puts("Alignment selections accepted by the printer.");
    return 0;
}
