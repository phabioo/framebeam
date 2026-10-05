package hub

import "fmt"

// Code is an error code in the API spec format (ErrorCode).
type Code string

// Error codes (subset of the spec that the hub produces).
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
	CodeSaveConflict       Code = "save_conflict"
	CodeSaveConflictStale  Code = "save_conflict_stale"
	CodePayloadTooLarge    Code = "payload_too_large"
)

// Error is a domain error with a spec code. errors.Is compares the code only.
type Error struct {
	Code    Code
	Message string
}

func (e *Error) Error() string { return fmt.Sprintf("%s: %s", e.Code, e.Message) }

// Is compares errors by code so that errors.Is(err, ErrNotFound) also matches with a custom message.
func (e *Error) Is(target error) bool {
	t, ok := target.(*Error)
	return ok && t.Code == e.Code
}

// Sentinel errors for errors.Is.
var (
	ErrBadRequest         = &Error{CodeBadRequest, "Invalid request"}
	ErrUnauthorized       = &Error{CodeUnauthorized, "Token missing or invalid"}
	ErrForbidden          = &Error{CodeForbidden, "Not allowed"}
	ErrNotFound           = &Error{CodeNotFound, "Not found"}
	ErrConflict           = &Error{CodeConflict, "Conflict"}
	ErrRateLimited        = &Error{CodeRateLimited, "Too many requests"}
	ErrDeviceRevoked      = &Error{CodeDeviceRevoked, "Device has been revoked"}
	ErrInvalidCredentials = &Error{CodeInvalidCredentials, "Invalid credentials"}
	ErrPairingExpired     = &Error{CodePairingExpired, "Pairing request expired"}
	// ErrSaveConflictStale: expected_revision is stale or the conflict is already resolved (nothing changed).
	ErrSaveConflictStale = &Error{CodeSaveConflictStale, "Slot changed since expected_revision; re-read the slot"}
	// ErrPayloadTooLarge: the upload exceeds MaxSaveBytes.
	ErrPayloadTooLarge = &Error{CodePayloadTooLarge, "Save exceeds 64 MiB"}
	// ErrAdminExists: an admin already exists (code conflict).
	ErrAdminExists = &Error{CodeConflict, "An admin already exists"}
)

func badRequest(format string, a ...any) *Error {
	return &Error{CodeBadRequest, fmt.Sprintf(format, a...)}
}

func conflict(msg string) *Error { return &Error{CodeConflict, msg} }

func internal(err error) error { return fmt.Errorf("hub: %w", err) }
