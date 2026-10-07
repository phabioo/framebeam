package hub

// SetSaveRetentionForTest changes the retention rules of a running service.
func (s *Service) SetSaveRetentionForTest(recent, daily, weekly int) {
	s.saveMu.Lock()
	defer s.saveMu.Unlock()
	s.saveKeep = saveRetention{recent: recent, daily: daily, weekly: weekly}
}
