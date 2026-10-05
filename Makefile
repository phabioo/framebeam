# Quiet check targets (details: scripts/check.sh). Prerequisite for check-client:
# scripts/bootstrap-vcpkg.sh has been run once.
.PHONY: check check-hub check-client build-hub generate fetch-core

check:
	@scripts/check.sh all

check-hub:
	@scripts/check.sh hub

check-client:
	@scripts/check.sh client

build-hub:
	@scripts/check.sh hub-build

generate:
	@scripts/check.sh generate

fetch-core:
	@scripts/fetch-melonds-ds.sh
