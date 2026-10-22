# syntax=docker/dockerfile:1
# Uzume server container image (ADR 0029 D1). Build context: the repository root.
#
#   docker build -f packaging/docker/Dockerfile -t uzume-server .
#   docker buildx build --platform linux/amd64,linux/arm64 -f packaging/docker/Dockerfile .
#
# Targets:
#   hub (default)   compiles the server from source (CGO off, pure-Go SQLite). The Go stage always runs on the build
#                   host's architecture and cross-compiles, so no emulation is needed for arm64.
#   hub-prebuilt    copies the linux binaries that CI already built and tested instead of compiling again (release.yml):
#                   --target hub-prebuilt --build-context prebuilt-bin=<dir with uzume-server-linux-{amd64,arm64}>
ARG GO_VERSION=1.26

FROM --platform=$BUILDPLATFORM golang:${GO_VERSION} AS build
ARG TARGETOS=linux
ARG TARGETARCH
ARG HUB_VERSION=dev
ARG HUB_CHANNEL=dev
ARG HUB_COMMIT=
WORKDIR /src/server
COPY server/go.mod server/go.sum ./
RUN go mod download
COPY server/ ./
RUN CGO_ENABLED=0 GOOS=$TARGETOS GOARCH=$TARGETARCH go build -trimpath \
      -ldflags "-s -w -X github.com/phabioo/uzume/server/internal/version.Version=${HUB_VERSION} -X github.com/phabioo/uzume/server/internal/version.Channel=${HUB_CHANNEL} -X github.com/phabioo/uzume/server/internal/version.Commit=${HUB_COMMIT}" \
      -o /out/uzume-server ./cmd/uzume-server

# Empty data directory, so that a new named volume starts out owned by the non-root user.
FROM --platform=$BUILDPLATFORM alpine:3 AS datadir
RUN mkdir /data

# Shared runtime base: distroless static (no shell, no package manager), non-root user 65532.
FROM gcr.io/distroless/static-debian12:nonroot AS base
# Only the container marker is set: the server defaults to /data in a container and to :8443, so a compose file that still
# sets UZUME_DATA_DIR / UZUME_LISTEN keeps winning.
ENV UZUME_SERVER_CONTAINER=1
COPY --from=datadir --chown=65532:65532 /data /data
WORKDIR /data
USER 65532:65532
# Everything the server stores (database, saves, firmware, ROM library, certificate, cores, plugins).
VOLUME ["/data"]
# Web interface and API (HTTPS, self-signed certificate unless -tls-cert/-tls-key), and the embedded TURN relay when
# -turn is on: control port UDP+TCP and the relay range (-turn-relay-ports, default 49160-49199/udp).
EXPOSE 8443/tcp 3478/udp 3478/tcp 49160-49199/udp
# No HEALTHCHECK: the image has no shell or curl and the server has no health command yet; check the web port from outside.
ENTRYPOINT ["/usr/local/bin/uzume-server"]

FROM base AS hub-prebuilt
ARG TARGETARCH
ARG HUB_VERSION=dev
ARG HUB_COMMIT=
LABEL org.opencontainers.image.title="Uzume server" \
      org.opencontainers.image.description="Self-hosted retro gaming server: ROM library, versioned saves, sessions" \
      org.opencontainers.image.source="https://github.com/phabioo/uzume" \
      org.opencontainers.image.licenses="GPL-3.0-or-later" \
      org.opencontainers.image.version="${HUB_VERSION}" \
      org.opencontainers.image.revision="${HUB_COMMIT}"
COPY --from=prebuilt-bin --chmod=0755 uzume-server-linux-${TARGETARCH} /usr/local/bin/uzume-server

# Last stage = default target.
FROM base AS hub
ARG HUB_VERSION=dev
ARG HUB_COMMIT=
LABEL org.opencontainers.image.title="Uzume server" \
      org.opencontainers.image.description="Self-hosted retro gaming server: ROM library, versioned saves, sessions" \
      org.opencontainers.image.source="https://github.com/phabioo/uzume" \
      org.opencontainers.image.licenses="GPL-3.0-or-later" \
      org.opencontainers.image.version="${HUB_VERSION}" \
      org.opencontainers.image.revision="${HUB_COMMIT}"
COPY --from=build /out/uzume-server /usr/local/bin/uzume-server
