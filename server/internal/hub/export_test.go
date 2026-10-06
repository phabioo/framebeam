package hub

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
