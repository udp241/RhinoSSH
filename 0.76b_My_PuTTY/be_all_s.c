/*
 * Linking module for PuTTY proper: list the available backends
 * including ssh, plus the serial backend.
 */

#include <stdio.h>
#include "putty.h"

/*
 * This appname is not strictly in the right place, since Plink
 * also uses this module. However, Plink doesn't currently use any
 * of the dialog-box sorts of things that make use of appname, so
 * it shouldn't do any harm here. I'm trying to avoid having to
 * have tiny little source modules containing nothing but
 * declarations of appname, for as long as I can...
 */
#if (defined MOD_PERSO) && (!defined FLJ)
char *appname = "Rhino";
#else
const char *const appname = "PuTTY";
#endif

const int be_default_protocol = PROT_SSH;

const struct BackendVtable *const backends[] = {
    &raw_backend,
    &telnet_backend,
    &rlogin_backend,
    &ssh_backend,
#ifdef MOD_ADB
    &adb_backend,
#endif
    &serial_backend,
    &sshconn_backend,
    NULL
};

#ifdef MOD_ADB
const size_t n_ui_backends = 6;
#else
const size_t n_ui_backends = 5;
#endif
