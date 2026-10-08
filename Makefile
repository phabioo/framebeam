# Quiet check targets (details: scripts/check.sh). Prerequisite for check-client:
# scripts/bootstrap-vcpkg.sh has been run once.
# HUB_VERSION / HUB_CHANNEL / HUB_COMMIT: ldflags of build-hub; HUB_VERSION is also the .deb version of package-hub-deb.
.PHONY: check check-hub check-client build-hub package-hub-deb generate notices fetch-core fetch-deps fetch-sdl3

check:
	@scripts/check.sh all

check-hub:
	@scripts/check.sh hub

check-client:
	@scripts/check.sh client

build-hub:
	@scripts/check.sh hub-build

package-hub-deb:
	@scripts/check.sh hub-deb

generate:
	@scripts/check.sh generate

# Regenerates server/THIRD-PARTY-NOTICES.txt (after go.mod/go.sum changes).
notices:
	@scripts/check.sh notices

fetch-core:
	@scripts/fetch-melonds-ds.sh

fetch-deps:
	@scripts/fetch-libdatachannel.sh

fetch-sdl3:
	@scripts/fetch-sdl3.sh
