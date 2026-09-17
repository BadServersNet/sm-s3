#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

#define SMEXT_CONF_NAME "S3"
#define SMEXT_CONF_DESCRIPTION "S3-compatible object storage client"
#define SMEXT_CONF_VERSION SM_S3_VERSION
#define SMEXT_CONF_AUTHOR "BuSheezy"
#define SMEXT_CONF_URL "https://github.com/BadServersNet/sm-s3"
#define SMEXT_CONF_LOGTAG "S3"
#define SMEXT_CONF_LICENSE "GPL"
#define SMEXT_CONF_DATESTRING __DATE__

#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

#define SMEXT_ENABLE_FORWARDSYS
#define SMEXT_ENABLE_HANDLESYS
#define SMEXT_ENABLE_LIBSYS
#define SMEXT_ENABLE_PLUGINSYS

#endif
