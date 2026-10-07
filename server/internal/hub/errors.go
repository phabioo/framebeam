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
	CodeSessionNotFound    Code = "session_not_found"
	CodeSessionForbidden   Code = "session_forbidden"
	CodeSessionFull        Code = "session_full"
	CodeSessionEnded       Code = "session_ended"
	CodeCapabilityMissing  Code = "capability_missing"
	CodeInviteInvalid      Code = "invite_invalid"
	CodeDisplayNameTaken   Code = "display_name_taken"
	CodeUserDisabled       Code = "user_disabled"
	CodeUploadsDisabled    Code = "uploads_disabled"
)

// Error is a domain error with a spec code. errors.Is compares the code only.
type Error struct {
	Code    Code
	Message string
	// ExistingGameID is set on a duplicate ROM upload (conflict) and names the library entry that holds it.
	ExistingGameID string
}

func (e *Error) Error() string { return fmt.Sprintf("%s: %s", e.Code, e.Message) }

// Is compares errors by code so that errors.Is(err, ErrNotFound) also matches with a custom message.
func (e *Error) Is(target error) bool {
	t, ok := target.(*Error)
	return ok && t.Code == e.Code
}

// Sentinel errors for errors.Is.
var (
	ErrBadRequest         = &Error{Code: CodeBadRequest, Message: "Invalid request"}
	ErrUnauthorized       = &Error{Code: CodeUnauthorized, Message: "Token missing or invalid"}
	ErrForbidden          = &Error{Code: CodeForbidden, Message: "Not allowed"}
	ErrNotFound           = &Error{Code: CodeNotFound, Message: "Not found"}
	ErrConflict           = &Error{Code: CodeConflict, Message: "Conflict"}
	ErrRateLimited        = &Error{Code: CodeRateLimited, Message: "Too many requests"}
	ErrDeviceRevoked      = &Error{Code: CodeDeviceRevoked, Message: "Device has been revoked"}
	ErrInvalidCredentials = &Error{Code: CodeInvalidCredentials, Message: "Invalid credentials"}
	ErrPairingExpired     = &Error{Code: CodePairingExpired, Message: "Pairing request expired"}
	// ErrSaveConflictStale: expected_revision is stale or the conflict is already resolved (nothing changed).
	ErrSaveConflictStale = &Error{Code: CodeSaveConflictStale, Message: "Slot changed since expected_revision; re-read the slot"}
	// ErrPayloadTooLarge: the upload exceeds MaxSaveBytes.
	ErrPayloadTooLarge  = &Error{Code: CodePayloadTooLarge, Message: "Save exceeds 64 MiB"}
	ErrSessionNotFound  = &Error{Code: CodeSessionNotFound, Message: "Session not found"}
	ErrSessionForbidden = &Error{Code: CodeSessionForbidden, Message: "Not allowed for this Session"}
	ErrSessionFull      = &Error{Code: CodeSessionFull, Message: "Session already has the maximum number of viewers"}
	ErrSessionEnded     = &Error{Code: CodeSessionEnded, Message: "Session has ended"}
	// ErrAdminExists: an admin already exists (code conflict).
	ErrAdminExists = &Error{Code: CodeConflict, Message: "An admin already exists"}
	// Invites, user state and uploads.
	ErrInviteInvalid    = &Error{Code: CodeInviteInvalid, Message: "Invite code is invalid, expired, used or revoked"}
	ErrDisplayNameTaken = &Error{Code: CodeDisplayNameTaken, Message: "Display name is already taken"}
	ErrUserDisabled     = &Error{Code: CodeUserDisabled, Message: "User has been disabled"}
	ErrUploadsDisabled  = &Error{Code: CodeUploadsDisabled, Message: "Uploads by users are disabled on this Hub"}
)

func badRequest(format string, a ...any) *Error {
	return &Error{Code: CodeBadRequest, Message: fmt.Sprintf(format, a...)}
}

func conflict(msg string) *Error { return &Error{Code: CodeConflict, Message: msg} }

func internal(err error) error { return fmt.Errorf("hub: %w", err) }

// Core packages (0.2).
const (
	CodeCorePackageNotFound  Code = "core_package_not_found"
	CodeCoreFileNotAvailable Code = "core_file_not_available"
)

var (
	ErrCorePackageNotFound  = &Error{Code: CodeCorePackageNotFound, Message: "Core package not found"}
	ErrCoreFileNotAvailable = &Error{Code: CodeCoreFileNotAvailable, Message: "Core file not available"}
)
