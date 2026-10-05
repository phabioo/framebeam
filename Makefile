# Leise Prüfziele (Details: scripts/check.sh). Voraussetzung für check-client:
# scripts/bootstrap-vcpkg.sh einmal ausgeführt.
.PHONY: check check-hub check-client build-hub generate

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
