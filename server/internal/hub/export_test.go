package hub

import (
	"context"

	"github.com/phabioo/uzume/server/internal/turnsrv"
)

// Test seams for the grace-timer generation check.

// ArmOwnerGraceForTest arms the owner grace timer and returns its generation.
func (s *Service) ArmOwnerGraceForTest(sessionID, deviceID string) uint64 {
	s.armOwnerGrace(sessionID, deviceID)
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	return rt.ownerTimers[sessionID].gen
}

// OwnerGraceExpiredForTest runs the timer callback of the given generation.
func (s *Service) OwnerGraceExpiredForTest(sessionID, deviceID string, gen uint64) {
	s.ownerGraceExpired(sessionID, deviceID, gen)
}

// OwnerGraceArmedForTest reports whether an owner grace timer is registered.
func (s *Service) OwnerGraceArmedForTest(sessionID string) bool {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	_, ok := rt.ownerTimers[sessionID]
	return ok
}

// ArmViewerGraceForTest arms the viewer grace timer and returns its generation.
func (s *Service) ArmViewerGraceForTest(viewerID, deviceID string) uint64 {
	s.armViewerGrace(viewerID, deviceID)
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	return rt.viewerTimers[viewerID].gen
}

// ViewerGraceExpiredForTest runs the timer callback of the given generation.
func (s *Service) ViewerGraceExpiredForTest(viewerID, deviceID string, gen uint64) {
	s.viewerGraceExpired(viewerID, deviceID, gen)
}

// ViewerGraceArmedForTest reports whether a viewer grace timer is registered.
func (s *Service) ViewerGraceArmedForTest(viewerID string) bool {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	_, ok := rt.viewerTimers[viewerID]
	return ok
}

// SetRawSettingForTest stores a raw settings value (legacy values).
func (s *Service) SetRawSettingForTest(ctx context.Context, key, value string) error {
	return s.setSetting(ctx, key, value)
}

// CleanTextForTest exposes cleanText.
func CleanTextForTest(v string, max int) string { return cleanText(v, max) }

// CheckCoresForTest exposes the handshake core check.
func CheckCoresForTest(reg []SystemEntry, cores []CoreReport) []Problem {
	return checkCores(reg, cores)
}

// CloudflareTURNURLs returns the TURN URLs of the cached Cloudflare credentials (tests only).
func (s *Service) CloudflareTURNURLs() []string {
	_, c := s.cf.current()
	if c == nil {
		return nil
	}
	return c.URLs
}

// ICEServersFor returns the ice_servers a device would get (tests only).
func (s *Service) ICEServersFor(reqHost, deviceID string) []string {
	ice, _ := s.iceFor(reqHost, deviceID)
	return ice
}

// ExternalCredentialsForTest returns the external TURN credentials issued for a device.
func (s *Service) ExternalCredentialsForTest(deviceID string) []turnsrv.Credentials {
	return s.externalFor(deviceID)
}

// SetFreeBytesForTest replaces the free-space probe of ROM uploads and returns the function that restores it.
func SetFreeBytesForTest(fn func(dir string) uint64) (restore func()) {
	old := freeBytesFn
	freeBytesFn = fn
	return func() { freeBytesFn = old }
}
