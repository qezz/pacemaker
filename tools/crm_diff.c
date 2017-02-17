
/* 
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <crm_internal.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/types.h>

#include <crm/crm.h>
#include <crm/msg_xml.h>
#include <crm/common/xml.h>
#include <crm/common/ipc.h>
#include <crm/cib.h>

/* *INDENT-OFF* */
static struct crm_option long_options[] = {
    /* Top-level Options */
    {"help",           0, 0, '?', "\t\tThis text"},
    {"version",        0, 0, '$', "\t\tVersion information"  },
    {"verbose",        0, 0, 'V', "\t\tIncrease debug output\n"},

    {"-spacer-",	1, 0, '-', "\nOriginal XML:"},
    {"original",	1, 0, 'o', "\tXML is contained in the named file"},
    {"original-string", 1, 0, 'O', "XML is contained in the supplied string"},

    {"-spacer-",	1, 0, '-', "\nOperation:"},
    {"new",		1, 0, 'n', "\tCompare the original XML to the contents of the named file"},
    {"new-string",      1, 0, 'N', "\tCompare the original XML to the contents of the supplied string"},
    {"patch",		1, 0, 'p', "\tPatch the original XML with the contents of the named file"},

    {"-spacer-", 1, 0, '-', "\nAdditional Options:"},
    {"cib",	 0, 0, 'c', "\t\tCompare/patch the inputs as a CIB (includes versions details)"},
    {"stdin",	 0, 0, 's', NULL, 1},
    {"no-version", 0, 0, 'u', "\tGenerate the difference without versions details"},
    {"-spacer-", 1, 0, '-', "\nExamples:", pcmk_option_paragraph},
    {"-spacer-", 1, 0, '-', "Obtain the two different configuration files by running cibadmin on the two cluster setups to compare:", pcmk_option_paragraph},
    {"-spacer-", 1, 0, '-', " cibadmin --query > cib-old.xml", pcmk_option_example},
    {"-spacer-", 1, 0, '-', " cibadmin --query > cib-new.xml", pcmk_option_example},
    {"-spacer-", 1, 0, '-', "Calculate and save the difference between the two files:", pcmk_option_paragraph},
    {"-spacer-", 1, 0, '-', " crm_diff --original cib-old.xml --new cib-new.xml > patch.xml", pcmk_option_example },
    {"-spacer-", 1, 0, '-', "Apply the patch to the original file:", pcmk_option_paragraph },
    {"-spacer-", 1, 0, '-', " crm_diff --original cib-old.xml --patch patch.xml > updated.xml", pcmk_option_example },
    {"-spacer-", 1, 0, '-', "Apply the patch to the running cluster:", pcmk_option_paragraph },
    {"-spacer-", 1, 0, '-', " cibadmin --patch patch.xml", pcmk_option_example },

    {0, 0, 0, 0}
};
/* *INDENT-ON* */

static void
print_patch(xmlNode *patch)
{
	crm_info("my trace");
    char *buffer = dump_xml_formatted(patch);

    printf("%s\n", crm_str(buffer));
    free(buffer);
    fflush(stdout);
}

static int
apply_patch(xmlNode *input, xmlNode *patch, gboolean as_cib)
{
	crm_info("my trace");
    int rc;
    xmlNode *output = copy_xml(input);

    rc = xml_apply_patchset(output, patch, as_cib);
    if (rc != pcmk_ok) {
        fprintf(stderr, "Could not apply patch: %s\n", pcmk_strerror(rc));
        free_xml(output);
        return rc;
    }

    if (output != NULL) {
        const char *version;
        char *buffer;

        print_patch(output);
        free_xml(output);

        version = crm_element_value(output, XML_ATTR_CRM_VERSION);
        buffer = calculate_xml_versioned_digest(output, FALSE, TRUE, version);
        crm_trace("Digest: %s\n", crm_str(buffer));
        free(buffer);
    }
    return pcmk_ok;
}

static void
log_patch_cib_versions(xmlNode *patch)
{
	crm_info("my trace");
    int add[] = { 0, 0, 0 };
    int del[] = { 0, 0, 0 };

    const char *fmt = NULL;
    const char *digest = NULL;

    xml_patch_versions(patch, add, del);
    fmt = crm_element_value(patch, "format");
    digest = crm_element_value(patch, XML_ATTR_DIGEST);

    if (add[2] != del[2] || add[1] != del[1] || add[0] != del[0]) {
        crm_info("Patch: --- %d.%d.%d %s", del[0], del[1], del[2], fmt);
        crm_info("Patch: +++ %d.%d.%d %s", add[0], add[1], add[2], digest);
    }
}

static void
strip_patch_cib_version(xmlNode *patch, const char **vfields, size_t nvfields)
{
	crm_info("my trace");
    int format = 1;

    crm_element_value_int(patch, "format", &format);
    if (format == 2) {
        xmlNode *version_xml = find_xml_node(patch, "version", FALSE);

        if (version_xml) {
            free_xml(version_xml);
        }

    } else {
        int i = 0;

        const char *tags[] = {
            XML_TAG_DIFF_REMOVED,
            XML_TAG_DIFF_ADDED,
        };

        for (i = 0; i < DIMOF(tags); i++) {
            xmlNode *tmp = NULL;
            int lpc;

            tmp = find_xml_node(patch, tags[i], FALSE);
            if (tmp) {
                for (lpc = 0; lpc < nvfields; lpc++) {
                    xml_remove_prop(tmp, vfields[lpc]);
                }

                tmp = find_xml_node(tmp, XML_TAG_CIB, FALSE);
                if (tmp) {
                    for (lpc = 0; lpc < nvfields; lpc++) {
                        xml_remove_prop(tmp, vfields[lpc]);
                    }
                }
            }
        }
    }
}

static int
generate_patch(xmlNode *object_1, xmlNode *object_2, const char *xml_file_2,
               gboolean as_cib, gboolean no_version)
{
	crm_info("my trace");
    xmlNode *output = NULL;

    const char *vfields[] = {
        XML_ATTR_GENERATION_ADMIN,
        XML_ATTR_GENERATION,
        XML_ATTR_NUMUPDATES,
    };

    /* If we're ignoring the version, make the version information
     * identical, so it isn't detected as a change. */
    if (no_version) {
        int lpc;

        for (lpc = 0; lpc < DIMOF(vfields); lpc++) {
            crm_copy_xml_element(object_1, object_2, vfields[lpc]);
        }
    }

    xml_track_changes(object_2, NULL, object_2, FALSE);
    xml_calculate_changes(object_1, object_2);
    crm_log_xml_debug(object_2, (xml_file_2? xml_file_2: "target"));

    output = xml_create_patchset(0, object_1, object_2, NULL, FALSE);

    xml_log_changes(LOG_INFO, __FUNCTION__, object_2);
    xml_accept_changes(object_2);

    if (output == NULL) {
        return pcmk_ok;
    }

    patchset_process_digest(output, object_1, object_2, as_cib);

    if (as_cib) {
        log_patch_cib_versions(output);

    } else if (no_version) {
        strip_patch_cib_version(output, vfields, DIMOF(vfields));
    }

    xml_log_patchset(LOG_NOTICE, __FUNCTION__, output);
    print_patch(output);
    free_xml(output);
    return 1;
}

int
main(int argc, char **argv)
{
	crm_info("my trace");
    gboolean apply = FALSE;
    gboolean raw_1 = FALSE;
    gboolean raw_2 = FALSE;
    gboolean use_stdin = FALSE;
    gboolean as_cib = FALSE;
    gboolean no_version = FALSE;
    int argerr = 0;
    int flag;
    int rc = pcmk_ok;
    xmlNode *object_1 = NULL;
    xmlNode *object_2 = NULL;
    const char *xml_file_1 = NULL;
    const char *xml_file_2 = NULL;

    int option_index = 0;

    crm_log_cli_init("crm_diff");
    crm_set_options(NULL, "original_xml operation [options]", long_options,
                    "crm_diff can compare two Pacemaker configurations (in XML format) to\n"
                    "produce a custom diff-like output, or apply such an output as a patch\n");

    if (argc < 2) {
        crm_help('?', EX_USAGE);
    }

    while (1) {
        flag = crm_get_option(argc, argv, &option_index);
        if (flag == -1)
            break;

        switch (flag) {
            case 'o':
                xml_file_1 = optarg;
                break;
            case 'O':
                xml_file_1 = optarg;
                raw_1 = TRUE;
                break;
            case 'n':
                xml_file_2 = optarg;
                break;
            case 'N':
                xml_file_2 = optarg;
                raw_2 = TRUE;
                break;
            case 'p':
                xml_file_2 = optarg;
                apply = TRUE;
                break;
            case 's':
                use_stdin = TRUE;
                break;
            case 'c':
                as_cib = TRUE;
                break;
            case 'u':
                no_version = TRUE;
                break;
            case 'V':
                crm_bump_log_level(argc, argv);
                break;
            case '?':
            case '$':
                crm_help(flag, EX_OK);
                break;
            default:
                printf("Argument %c (0%o) is not (yet?) supported\n", flag, flag);
                ++argerr;
                break;
        }
    }

    if (optind < argc) {
        printf("non-option ARGV-elements: ");
        while (optind < argc)
            printf("%s ", argv[optind++]);
        printf("\n");
    }

    if (optind > argc) {
        ++argerr;
    }

    if (argerr) {
        crm_help('?', EX_USAGE);
    }

    if (apply && no_version) {
        fprintf(stderr, "warning: -u/--no-version ignored with -p/--patch\n");
    } else if (as_cib && no_version) {
        fprintf(stderr, "error: -u/--no-version incompatible with -c/--cib\n");
        return 1;
    }

    if (raw_1) {
        object_1 = string2xml(xml_file_1);

    } else if (use_stdin) {
        fprintf(stderr, "Input first XML fragment:");
        object_1 = stdin2xml();

    } else if (xml_file_1 != NULL) {
        object_1 = filename2xml(xml_file_1);
    }

    if (raw_2) {
        object_2 = string2xml(xml_file_2);

    } else if (use_stdin) {
        fprintf(stderr, "Input second XML fragment:");
        object_2 = stdin2xml();

    } else if (xml_file_2 != NULL) {
        object_2 = filename2xml(xml_file_2);
    }

    if (object_1 == NULL) {
        fprintf(stderr, "Could not parse the first XML fragment\n");
        return 1;
    }
    if (object_2 == NULL) {
        fprintf(stderr, "Could not parse the second XML fragment\n");
        return 1;
    }

    if (apply) {
        rc = apply_patch(object_1, object_2, as_cib);
    } else {
        rc = generate_patch(object_1, object_2, xml_file_2, as_cib, no_version);
    }

    free_xml(object_1);
    free_xml(object_2);
    return rc;
}
