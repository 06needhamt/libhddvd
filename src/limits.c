#include "internal.h"
#include <string.h>

void hddvd_config_limits_default(hddvd_config_limits *limits)
{
    if (!limits)
        return;
    memset(limits, 0, sizeof(*limits));
    limits->max_archive_members = 4096;
    limits->max_archive_member_bytes = 128u * 1024u * 1024u;
    limits->max_resource_bytes = 128u * 1024u * 1024u;
    limits->max_xml_nodes = 65536;
    limits->max_xml_depth = 128;
    limits->max_xml_attributes = 262144;
    limits->max_references = 4096;
    limits->max_playlist_items = 65536;
    limits->max_images = 256;
    limits->max_encoded_image_bytes = 64u * 1024u * 1024u;
    limits->max_decoded_pixels = 1920u * 1080u * 8u;
    limits->max_image_dimension = 8192;
    limits->max_quads = 4096;
    limits->max_script_strings = 4096;
    limits->max_script_functions = 4096;
    limits->max_script_instructions = 1000000;
    limits->max_script_bytes = 64u * 1024u * 1024u;
    limits->max_script_heap_bytes = 16u * 1024u * 1024u;
    limits->max_script_recursion = 64;
    limits->max_timers = 256;
    limits->max_queued_events = 1024;
}

int hddvd_config_limits_valid(const hddvd_config_limits *limits)
{
    return limits && limits->max_archive_members && limits->max_archive_member_bytes &&
           limits->max_resource_bytes && limits->max_xml_nodes && limits->max_xml_depth &&
           limits->max_xml_attributes && limits->max_references && limits->max_playlist_items &&
           limits->max_images && limits->max_encoded_image_bytes && limits->max_decoded_pixels &&
           limits->max_image_dimension && limits->max_quads && limits->max_script_strings &&
           limits->max_script_functions && limits->max_script_instructions &&
           limits->max_script_bytes && limits->max_script_heap_bytes &&
           limits->max_script_recursion && limits->max_timers && limits->max_queued_events;
}
