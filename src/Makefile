#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := glasshouse

CFLAGS += -I$(abspath ..) -DHOMEKIT_SHORT_APPLE_UUIDS

EXTRA_COMPONENT_DIRS += \
  $(abspath ../components/common) \
  $(abspath ../components/esp-32)

include $(IDF_PATH)/make/project.mk
