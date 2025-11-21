ONEDRIVE_ENV_FILE ?= .onedrive.env
-include $(ONEDRIVE_ENV_FILE)

# Require ID and tenant; secret may be intentionally empty for public clients.
REQUIRED_ONEDRIVE_VARS := ONEDRIVE_CLIENT_ID ONEDRIVE_TENANT
MISSING_VARS := $(strip $(foreach var,$(REQUIRED_ONEDRIVE_VARS),$(if $(filter undefined,$(origin $(var))),$(var),)))
$(if $(MISSING_VARS),$(error Missing variables: $(MISSING_VARS). Define them in $(ONEDRIVE_ENV_FILE).))

CMAKE_OPTIONS := -DONEDRIVE_CLIENT_ID=$(ONEDRIVE_CLIENT_ID) -DONEDRIVE_CLIENT_SECRET=$(ONEDRIVE_CLIENT_SECRET) -DONEDRIVE_TENANT=$(ONEDRIVE_TENANT)

.PHONY: build
build:
	kde-builder kio-onedrive -r \
		--cmake-options="$(CMAKE_OPTIONS)"
