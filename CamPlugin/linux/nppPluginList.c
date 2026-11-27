/* nppPluginList.so - plugin list for Camelion++ (Linux counterpart of nppPluginList.dll)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * Camelion++ itself reads the list straight from the .npp_pluginlist /
 * .npp_pluginlist_sig ELF sections and never loads this library. The
 * functions below exist for other tools that prefer dlopen().
 */
#include <stddef.h>

#define EXPORT __attribute__((visibility("default")))

extern const char nppPluginList_json_begin[], nppPluginList_json_end[];
extern const unsigned char nppPluginList_sig_begin[], nppPluginList_sig_end[];

EXPORT const char* nppPluginList_json(size_t* length)
{
    if (length) *length = (size_t)(nppPluginList_json_end - nppPluginList_json_begin);
    return nppPluginList_json_begin;
}

EXPORT const unsigned char* nppPluginList_signature(size_t* length)
{
    if (length) *length = (size_t)(nppPluginList_sig_end - nppPluginList_sig_begin);
    return nppPluginList_sig_begin;
}

EXPORT const char* nppPluginList_version(void) { return NPPPLUGINLIST_VERSION; }
EXPORT const char* nppPluginList_arch(void) { return NPPPLUGINLIST_ARCH; }
EXPORT int nppPluginList_isSigned(void) { return NPPPLUGINLIST_SIGNED; }
