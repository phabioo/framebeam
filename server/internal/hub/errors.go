package hub

import "fmt"

// Code ist ein Fehlercode im Format der API-Spec (ErrorCode).
type Code string

// Fehlercodes (Teilmenge der Spec, die der Hub erzeugt).
const (
	CodeBadRequest         Code = "bad_request"
	CodeUnauthorized       Code = "unauthorized"
	CodeForbidden          Code = "forbidden"
	CodeNotFound           Code = "not_found"
	CodeConflict           Code = "conflict"
	CodeRateLimited        Code = "rate_limited"
	CodeInternal           Code = "internal"
	CodeDeviceRevoked      Code = "device_revoked"
	CodeInvalidCredentials Code = "invalid_credentials"
	CodePairingExpired     Code = "pairing_expired"
)

// Error ist ein fachlicher Fehler mit Spec-Code. errors.Is vergleicht nur den Code.
type Error struct {
	Code    Code
	Message string
}

func (e *Error) Error() string { return fmt.Sprintf("%s: %s", e.Code, e.Message) }

// Is vergleicht Fehler über den Code, sodass errors.Is(err, ErrNotFound) auch bei eigener Meldung greift.
func (e *Error) Is(target error) bool {
	t, ok := target.(*Error)
	return ok && t.Code == e.Code
}

// Sentinel-Fehler für errors.Is.
var (
	ErrBadRequest         = &Error{CodeBadRequest, "Ungültige Anfrage"}
	ErrUnauthorized       = &Error{CodeUnauthorized, "Token fehlt oder ungültig"}
	ErrForbidden          = &Error{CodeForbidden, "Nicht erlaubt"}
	ErrNotFound           = &Error{CodeNotFound, "Nicht gefunden"}
	ErrConflict           = &Error{CodeConflict, "Konflikt"}
	ErrRateLimited        = &Error{CodeRateLimited, "Zu viele Anfragen"}
	ErrDeviceRevoked      = &Error{CodeDeviceRevoked, "Gerät wurde widerrufen"}
	ErrInvalidCredentials = &Error{CodeInvalidCredentials, "Zugangsdaten ungültig"}
	ErrPairingExpired     = &Error{CodePairingExpired, "Pairing-Anfrage abgelaufen"}
	// ErrAdminExists: es gibt bereits einen Admin (Code conflict).
	ErrAdminExists = &Error{CodeConflict, "Es existiert bereits ein Admin"}
)

func badRequest(format string, a ...any) *Error {
	return &Error{CodeBadRequest, fmt.Sprintf(format, a...)}
}

func conflict(msg string) *Error { return &Error{CodeConflict, msg} }

func internal(err error) error { return fmt.Errorf("hub: %w", err) }
