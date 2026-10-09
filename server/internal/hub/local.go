package hub

import (
	"context"
	"errors"
	"strconv"
	"strings"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/config"
)

// Local setup (0.9): the Player on the same PC sets up or pairs with the Hub without an admin approving the
// request. The HTTP layer only lets loopback callers in; the rules below do not depend on that.

// FeatureLocalSetupV1 marks a Hub that offers the /api/v1/local endpoints.
const FeatureLocalSetupV1 = "local_setup_v1"

// CodeImportDirUnreadable: the import folder is not an absolute, existing, readable directory (400).
const CodeImportDirUnreadable Code = "import_dir_unreadable"

// LocalStatus is the state of the local setup as reported to the Player.
type LocalStatus struct {
	AdminExists    bool
	NetworkSharing bool
	ImportDir      string
	HubVersion     string
}

// LocalDefaults are the values of the flags: they apply while no value is stored.
type LocalDefaults struct {
	NetworkSharing bool
	ImportDir      string
}

// SetLocalDefaults sets the values used when nothing is stored (done at startup).
func (s *Service) SetLocalDefaults(d LocalDefaults) {
	s.localMu.Lock()
	s.localDef = d
	s.localMu.Unlock()
}

// LocalInput is the body of the local setup and pair requests.
type LocalInput struct {
	Username, Password string
	Device             PairingInput // DeviceName, DeviceID, Platform, Arch, PlayerVersion, ProtocolVersion, RemoteAddr
}

// LocalStatus returns the status object (the settings as stored, else the flag values).
func (s *Service) LocalStatus(ctx context.Context) (LocalStatus, error) {
	has, err := s.HasAdmin(ctx)
	if err != nil {
		return LocalStatus{}, err
	}
	stored, err := s.NetOverrides(ctx)
	if err != nil {
		return LocalStatus{}, err
	}
	s.localMu.Lock()
	def := s.localDef
	s.localMu.Unlock()
	st := LocalStatus{AdminExists: has, NetworkSharing: def.NetworkSharing, ImportDir: def.ImportDir, HubVersion: s.Info().HubVersion}
	if v, ok := stored[config.NetSharing]; ok {
		if b, err := strconv.ParseBool(v); err == nil {
			st.NetworkSharing = b
		}
	}
	if v := strings.TrimSpace(stored[config.NetImportDir]); v != "" {
		st.ImportDir = v
	}
	return st, nil
}

// SeedNetworkSharing stores the flag value as the persisted setting when none is stored yet.
func (s *Service) SeedNetworkSharing(ctx context.Context, on bool) error {
	_, ok, err := s.getSetting(ctx, netSettingPrefix+config.NetSharing)
	if err != nil || ok {
		return err
	}
	return s.SetNetOverride(ctx, config.NetSharing, strconv.FormatBool(on))
}

// UpdateLocalSettings stores the given settings. changed reports whether a value differs from before (the caller
// then restarts the server, because binding and the import folder are read at startup).
func (s *Service) UpdateLocalSettings(ctx context.Context, sharing *bool, importDir *string) (LocalStatus, bool, error) {
	before, err := s.LocalStatus(ctx)
	if err != nil {
		return LocalStatus{}, false, err
	}
	if importDir != nil {
		if err := config.CheckImportDir(*importDir); err != nil {
			return LocalStatus{}, false, &Error{Code: CodeImportDirUnreadable, Message: "import_dir_unreadable: " + err.Error()}
		}
	}
	if sharing != nil {
		if err := s.SetNetOverride(ctx, config.NetSharing, strconv.FormatBool(*sharing)); err != nil {
			return LocalStatus{}, false, err
		}
	}
	if importDir != nil {
		if err := s.SetNetOverride(ctx, config.NetImportDir, *importDir); err != nil {
			return LocalStatus{}, false, err
		}
	}
	after, err := s.LocalStatus(ctx)
	return after, after.NetworkSharing != before.NetworkSharing || after.ImportDir != before.ImportDir, err
}

// pairLocally registers the device for userID the way an approved pairing request does: the request is created,
// approved and polled in one go, so devices, tokens and the pairing table are handled by the normal code.
func (s *Service) pairLocally(ctx context.Context, userID string, in PairingInput) (PairingResult, error) {
	c, err := s.CreatePairingRequest(ctx, in)
	if err != nil {
		return PairingResult{}, err
	}
	if err := s.ApprovePairing(ctx, c.RequestID, userID); err != nil {
		_ = s.DenyPairing(ctx, c.RequestID)
		return PairingResult{}, err
	}
	res, err := s.PollPairing(ctx, c.RequestID, c.PollToken)
	if err != nil {
		return PairingResult{}, err
	}
	if res.Status != PairingApproved {
		return PairingResult{}, conflict("This device ID already belongs to a device of another user")
	}
	return res, nil
}

// LocalSetup creates the first admin and pairs the calling Player as that admin. ErrAdminExists when an admin
// already exists.
func (s *Service) LocalSetup(ctx context.Context, in LocalInput) (PairingResult, User, error) {
	if err := s.validLocalDevice(in.Device); err != nil {
		return PairingResult{}, User{}, err
	}
	if err := s.rateLimitRedeem(in.Device.RemoteAddr); err != nil {
		return PairingResult{}, User{}, err
	}
	u, err := s.CreateAdmin(ctx, strings.TrimSpace(in.Username), in.Password)
	if err != nil {
		return PairingResult{}, User{}, err
	}
	res, err := s.pairLocally(ctx, u.ID, in.Device)
	return res, u, err
}

// LocalPair pairs the calling Player as an enabled admin whose credentials are given (ErrInvalidCredentials
// otherwise).
func (s *Service) LocalPair(ctx context.Context, in LocalInput) (PairingResult, User, error) {
	if err := s.validLocalDevice(in.Device); err != nil {
		return PairingResult{}, User{}, err
	}
	if err := s.rateLimitRedeem(in.Device.RemoteAddr); err != nil {
		return PairingResult{}, User{}, err
	}
	u, err := s.VerifyPassword(ctx, strings.TrimSpace(in.Username), in.Password)
	if err == nil && (u.Role != RoleAdmin || u.Disabled()) {
		err = ErrInvalidCredentials
	}
	if err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			s.noteRedeemFailure()
		}
		return PairingResult{}, User{}, err
	}
	res, err := s.pairLocally(ctx, u.ID, in.Device)
	return res, u, err
}

// validLocalDevice applies the field checks of CreatePairingRequest up front, so that a bad device never leaves a
// freshly created admin behind.
func (s *Service) validLocalDevice(in PairingInput) error {
	if _, err := uuid.Parse(in.DeviceID); err != nil {
		return badRequest("device_id must be a UUID")
	}
	if !validPairingField(in.DeviceName, 100) || !validPairingField(in.Platform, 50) ||
		!validPairingField(in.Arch, 50) || !validPairingField(in.PlayerVersion, 50) || in.ProtocolVersion < 1 {
		return badRequest("Required field missing or too long")
	}
	return nil
}
