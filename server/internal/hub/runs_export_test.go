package hub

import "context"

// SetRunLastSeqForTest moves the event counter of a run (to reach the event limit without 50 000 appends).
func SetRunLastSeqForTest(s *Service, runID string, n int64) error {
	_, err := s.db.ExecContext(context.Background(), `UPDATE runs SET last_seq = ? WHERE id = ?`, n, runID)
	return err
}
