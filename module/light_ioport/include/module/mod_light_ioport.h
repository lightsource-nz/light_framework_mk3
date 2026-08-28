#ifndef _MOD_LIGHT_IOPORT_H
#define _MOD_LIGHT_IOPORT_H

#include <light.h>

// the repository's version, derived from its git tags -- see light_project_version(LIGHT)
#include <light_version.h>
#define LIGHT_IOPORT_VERSION_STR           LIGHT_VERSION_STRING

#define LIGHT_IOPORT_INFO_STR              "light_ioport v" LIGHT_IOPORT_VERSION_STR

Light_Module_Declare(light_ioport);

#endif
