package hub

import (
	"context"
	"database/sql"
	"errors"
	"time"
)

// RunningSession names the active Session of a user that runs a game: the device that hosts it and since when.
type RunningSession struct {
	DeviceName string
	Since      time.Time
}

// RunningSessionFor returns the active hosted Session of the user for the game; ok is false when none runs. The
// web interface uses it to explain why restoring and uploading saves is not possible right now.
func (s *Service) RunningSessionFor(ctx context.Context, userID, gameID string) (rs RunningSession, ok bool, err error) {
	var created int64
	err = s.db.QueryRowContext(ctx, `SELECT d.name, s.created_at FROM sessions s JOIN devices d ON d.id = s.owner_device_id
		WHERE s.ended_at IS NULL AND s.owner_user_id = ? AND s.game_id = ? ORDER BY s.created_at DESC LIMIT 1`, userID, gameID).Scan(&rs.DeviceName, &created)
	if errors.Is(err, sql.ErrNoRows) {
		return RunningSession{}, false, nil
	}
	if err != nil {
		return RunningSession{}, false, internal(err)
	}
	rs.Since = time.Unix(created, 0)
	return rs, true, nil
}
