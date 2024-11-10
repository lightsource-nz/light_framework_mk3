#ifndef _MOD_LIGHT_CLI_H
#define _MOD_LIGHT_CLI_H

#include <light.h>

// TODO implement version fields properly
#define LIGHT_CLI_VERSION_STR           "0.1.0"

#define LIGHT_CLI_INFO_STR              "Light USB Host v" LUSB_HOST_MIDI_VERSION_STR

Light_Module_Declare(light_cli)

#endif