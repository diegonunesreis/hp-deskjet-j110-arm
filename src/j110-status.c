#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "hpmud.h"

#define PROBE_CAPACITY 8192
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
            if (end) {
                *end = '\0';
                return uri;
            }
            return uri;
        }
        if (!next)
            break;
        line = next + 1;
    }
    return NULL;
}

int main(void)
{
    char probe_output[PROBE_CAPACITY];
    int count = 0;
    int bytes_read = 0;
    int device = 0;
    unsigned int status = 0;
    enum HPMUD_RESULT result;
    char *uri;

    result = hpmud_probe_devices(HPMUD_BUS_USB, probe_output,
                                 sizeof(probe_output), &count, &bytes_read);
    if (result != HPMUD_R_OK) {
        fprintf(stderr, "USB discovery failed (HPLIP result %d).\n", result);
        return 1;
    }

    uri = find_j110_uri(probe_output);
    if (!uri) {
        fprintf(stderr, "No supported Deskjet 1000 J110 was found on USB.\n");
        return 2;
    }

    result = hpmud_open_device(uri, HPMUD_RAW_MODE, &device);
    if (result != HPMUD_R_OK) {
        fprintf(stderr, "Could not open the J110 USB transport (HPLIP result %d).\n", result);
        return 3;
    }

    result = hpmud_get_device_status(device, &status);
    hpmud_close_device(device);
    if (result != HPMUD_R_OK) {
        fprintf(stderr, "Could not read USB printer status (HPLIP result %d).\n", result);
        return 4;
    }

    printf("Deskjet 1000 J110 USB status: 0x%02x\n", status & 0xff);
    return 0;
}
