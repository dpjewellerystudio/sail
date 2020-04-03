#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin.h"
#include "ini.h"
#include "utils.h"

int sail_plugin_info_alloc(struct sail_plugin_info **plugin_info) {

    *plugin_info = (struct sail_plugin_info *)malloc(sizeof(struct sail_plugin_info));

    if (*plugin_info == NULL) {
        return ENOMEM;
    }

    (*plugin_info)->layout      = 0;
    (*plugin_info)->version     = NULL;
    (*plugin_info)->description = NULL;
    (*plugin_info)->extensions  = NULL;
    (*plugin_info)->mime_types  = NULL;
    (*plugin_info)->magic       = NULL;

    return 0;
}

void sail_plugin_info_destroy(struct sail_plugin_info *plugin_info) {

    if (plugin_info == NULL) {
        return;
    }

    if (plugin_info->version != NULL) {
        free(plugin_info->version);
    }
    if (plugin_info->description != NULL) {
        free(plugin_info->description);
    }
    if (plugin_info->extensions != NULL) {
        free(plugin_info->extensions);
    }
    if (plugin_info->mime_types != NULL) {
        free(plugin_info->mime_types);
    }
    if (plugin_info->magic != NULL) {
        free(plugin_info->magic);
    }

    free(plugin_info);
}

static int inih_handler(void *data, const char *section, const char *name, const char *value) {

    (void)section;

    struct sail_plugin_info *plugin_info = (struct sail_plugin_info *)data;

    int res;

    if (strcmp(name, "layout") == 0) {
        plugin_info->layout = atoi(value);
        return 1;
    }

    if (plugin_info->layout == 0) {
        fprintf(stderr, "SAIL: Plugin layout version is unknown\n");
        return 0;
    }

    if (plugin_info->layout == 1) {
        if (strcmp(name, "version") == 0) {
            if ((res = sail_strdup(value, &plugin_info->version)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "description") == 0) {
            if ((res = sail_strdup(value, &plugin_info->description)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "extensions") == 0) {
            if ((res = sail_strdup(value, &plugin_info->extensions)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "mime-types") == 0) {
            if ((res = sail_strdup(value, &plugin_info->mime_types)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "magic") == 0) {
            if ((res = sail_strdup(value, &plugin_info->magic)) != 0) {
                return 0;
            }
        } else {
            fprintf(stderr, "SAIL: Unsupported plugin configuraton key '%s'\n", name);
            return 0;  /* error */
        }
    } else {
        fprintf(stderr, "SAIL: Unsupported plugin layout version %d\n", plugin_info->layout);
    }

    return 1;
}

int sail_plugin_read_info(const char *file, struct sail_plugin_info **plugin_info) {

    if (file == NULL) {
        return EINVAL;
    }

    int res;

    if ((res = sail_plugin_info_alloc(plugin_info)) != 0) {
        return res;
    }

    /*
     * Returns 0 on success, line number of first error on parse error (doesn't
     * stop on first error), -1 on file open error, or -2 on memory allocation
     * error (only when INI_USE_STACK is zero).
     */
    if (ini_parse(file, inih_handler, *plugin_info) != 0) {
        sail_plugin_info_destroy(*plugin_info);
        return EIO;
    }

    return 0;
}