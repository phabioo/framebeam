// Package adminhost is the boundary between the admin REST API and the process-level facts of the running server
// (the configuration it started with, the TURN server, the certificate, the restart hook). The web interface holds
// those facts and implements Host; the API only calls it, so both use the same validation and apply code.
package adminhost

import (
	"context"
	"errors"
	"net/url"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/turnsrv"
)

// ValidationError is a refused input; Msg is meant for the admin ("Port 443 is in use").
type ValidationError struct{ Msg string }

func (e *ValidationError) Error() string { return e.Msg }

// ErrUnsupported: the server cannot do this (no restart hook, no self-generated certificate).
var ErrUnsupported = errors.New("not supported by this server")

// NetState is the network configuration as it is meant to be, next to what runs now.
type NetState struct {
	// Base is server.env/flags without saved values, Running what the server started with, Desired both combined.
	Base, Running, Desired config.Config
	// Stored are the raw values saved on the web interface / API, by key.
	Stored map[string]string
	// Pending are the labels of saved settings that need a restart; Issues the saved values that failed at startup.
	Pending, Issues []string
	CanRestart      bool
	// TURN is the built-in relay (nil: off) with the router port forwards it needs.
	TURN         *turnsrv.Status
	TURNForwards []string
}

// Security describes transport and certificate.
type Security struct {
	TLS          bool
	TrustedProxy []string
	Fingerprint  string
	CertSource   string
	NotAfter     time.Time
	CanRenew     bool
}

// Restart tells where the hub will be after a restart.
type Restart struct {
	URL         string // empty when the address is unknown
	PortChanged bool   // the listen port changes: clients cannot reconnect by themselves
}

// Host is implemented by the web interface server.
type Host interface {
	NetState(ctx context.Context) (NetState, error)
	// SaveNet validates and stores one network setting and applies it live when it can be. form carries the fields of
	// the web form: value, or min and max (relay ports), or op and url (STUN servers). hub.ErrNotFound for an unknown
	// key, *ValidationError for a refused value.
	SaveNet(ctx context.Context, key string, form url.Values) error
	// ResetNet deletes the saved value so server.env/flags apply again.
	ResetNet(ctx context.Context, key string) error
	// PrepareRestart answers where the hub will be; the returned func asks the process to restart and must be called
	// after the response is written. ErrUnsupported without a restart hook.
	PrepareRestart(ctx context.Context, admin string) (Restart, func(), error)
	Security() Security
	// RenewCert replaces the self-generated certificate; ErrUnsupported otherwise.
	RenewCert(admin string) (fingerprint string, notAfter time.Time, err error)
	// ImportFolder is the library import folder in use and the size limit of one imported file.
	ImportFolder() (dir string, maxBytes int64)
	// RunningConfig is the effective configuration for the diagnostics bundle.
	RunningConfig() config.Config
}
