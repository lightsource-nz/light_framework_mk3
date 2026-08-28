#ifndef _MOD_LIGHT_CLI_H
#define _MOD_LIGHT_CLI_H

#include <light.h>

// the repository's version, derived from its git tags -- see light_project_version(LIGHT)
#include <light_version.h>
#define LIGHT_CLI_VERSION_STR           LIGHT_VERSION_STRING

#define LIGHT_CLI_INFO_STR              "Light CLI v" LIGHT_CLI_VERSION_STR

Light_Module_Declare(light_cli);

#endif