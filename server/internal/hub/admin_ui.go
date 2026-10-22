package hub

import (
	"context"
	"errors"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/auth"
)

// Admin operations the web interface needs (0.14 UI round 3, designs 3m-2, 3n-2, 4jh-3). None of them is part of
// the app API.

// ---- Users ----

// RenameUser changes the display name of a user (1 to MaxDisplayNameLen characters, unique per server, case-insensitive).
// The username (sign-in name) and the user id stay as they are.
func (s *Service) RenameUser(ctx context.Context, userID, displayName string) (err error) {
	defer s.publishOK(&err, TopicUsers, TopicClients, TopicSaves, TopicLibrary)
	name, err := cleanDisplayName(displayName)
	if err != nil {
		return err
	}
	res, err := s.db.ExecContext(ctx, `UPDATE users SET display_name = ? WHERE id = ?`, name, userID)
	if err != nil {
		if isUnique(err) {
			return ErrDisplayNameTaken
		}
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return nil
}

// SetUserRole changes the role of a user. Making a user an admin needs a password (the web interface signs in with
// username and password, MinPasswordLen characters); the user must not be disabled. Making an admin a regular user
// keeps the password (a member may have one, ADR 0030 D5) and ends their web sessions; the actor cannot demote themselves and the last admin stays.
func (s *Service) SetUserRole(ctx context.Context, actorID, userID string, role Role, password string) (err error) {
	defer s.publishOK(&err, TopicUsers, TopicClients)
	u, err := s.GetUser(ctx, userID)
	if err != nil {
		return err
	}
	switch role {
	case RoleAdmin:
		if u.Role == RoleAdmin {
			return nil
		}
		if u.Disabled() {
			return badRequest("Enable the user first")
		}
		if len(password) < MinPasswordLen {
			return badRequest("Password must be at least %d characters long", MinPasswordLen)
		}
		hash, err := auth.HashPassword(password, s.params)
		if err != nil {
			return internal(err)
		}
		_, err = s.db.ExecContext(ctx, `UPDATE users SET role = 'admin', password_hash = ?, password_changed_at = ?, must_change_password = 0
			WHERE id = ? AND role = 'user'`, hash, s.now().Unix(), userID)
		return internal2(err)
	case RoleUser:
		if u.Role != RoleAdmin {
			return nil
		}
		if userID == actorID {
			return &Error{Code: CodeForbidden, Message: "You cannot remove your own admin role"}
		}
		tx, err := s.db.BeginTx(ctx, nil)
		if err != nil {
			return internal(err)
		}
		defer tx.Rollback()
		var admins int
		if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE role = 'admin'`).Scan(&admins); err != nil {
			return internal(err)
		}
		if admins < 2 {
			return &Error{Code: CodeForbidden, Message: "The last admin cannot be changed"}
		}
		if _, err := tx.ExecContext(ctx, `UPDATE users SET role = 'user' WHERE id = ?`, userID); err != nil {
			return internal(err)
		}
		if _, err := tx.ExecContext(ctx, `DELETE FROM web_sessions WHERE user_id = ?`, userID); err != nil {
			return internal(err)
		}
		return internal2(tx.Commit())
	}
	return badRequest("role must be admin or user")
}

// CreateUserWithCode creates a regular user together with a one-time pairing code (Users > Create user). The code
// pairs a device with exactly this user when an app redeems it; it is returned only here.
func (s *Service) CreateUserWithCode(ctx context.Context, createdBy, displayName string, ttl time.Duration) (_ User, _ Invite, _ string, err error) {
	name, err := cleanDisplayName(displayName)
	if err != nil {
		return User{}, Invite{}, "", err
	}
	username := slugUsername(name)
	for i := 2; ; i++ {
		var n int
		if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE username = ? COLLATE NOCASE`, username).Scan(&n); err != nil {
			return User{}, Invite{}, "", internal(err)
		}
		if n == 0 {
			break
		}
		username = slugUsername(name) + "-" + strconv.Itoa(i)
	}
	u, err := s.CreateUser(ctx, username, name)
	if err != nil {
		if errors.Is(err, ErrConflict) {
			return User{}, Invite{}, "", ErrDisplayNameTaken
		}
		return User{}, Invite{}, "", err
	}
	inv, code, err := s.CreateCodeForUser(ctx, createdBy, u.ID, ttl)
	return u, inv, code, err
}

// CreateCodeForUser creates a one-time pairing code for an existing regular user. The first device that redeems
// it is trusted directly (the admin hands the code over personally).
func (s *Service) CreateCodeForUser(ctx context.Context, createdBy, userID string, ttl time.Duration) (_ Invite, _ string, err error) {
	defer s.publishOK(&err, TopicUsers)
	valid := false
	for _, t := range InviteTTLs {
		valid = valid || t == ttl
	}
	if !valid {
		return Invite{}, "", badRequest("Expiry must be 15 minutes, 1 hour, 24 hours or 7 days")
	}
	u, err := s.GetUser(ctx, userID)
	if err != nil {
		return Invite{}, "", err
	}
	if u.Disabled() || u.Role == RoleAdmin {
		return Invite{}, "", badRequest("A pairing code is for an enabled regular user")
	}
	code, err := newInviteCode()
	if err != nil {
		return Invite{}, "", internal(err)
	}
	now := s.now().Truncate(time.Second).UTC()
	inv := Invite{ID: uuid.NewString(), AuthorizeDevice: true, Status: InviteActive, CreatedAt: now, ExpiresAt: now.Add(ttl)}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO invites(id, code_hash, authorize_device, status, created_by, created_at, expires_at, for_user_id)
		VALUES (?,?,1,'active',?,?,?,?)`, inv.ID, hashInviteCode(code), createdBy, now.Unix(), inv.ExpiresAt.Unix(), userID); err != nil {
		return Invite{}, "", internal(err)
	}
	return inv, code, nil
}

// ---- Devices ----

// RenameDevice changes the name a device is shown with (1 to 100 characters, no control characters).
func (s *Service) RenameDevice(ctx context.Context, deviceID, name string) (err error) {
	defer s.publishOK(&err, TopicClients)
	name = strings.TrimSpace(strings.Map(func(r rune) rune {
		if r < 0x20 || r == 0x7f {
			return -1
		}
		return r
	}, name))
	if name == "" || len([]rune(name)) > 100 {
		return badRequest("Device name must be 1 to 100 characters long")
	}
	res, err := s.db.ExecContext(ctx, `UPDATE devices SET name = ? WHERE id = ?`, name, deviceID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return nil
}

// DeviceOnline reports whether the device has an open WSS connection right now.
func (s *Service) DeviceOnline(deviceID string) bool { return s.deviceConnected(deviceID) }

// ---- Runs ----

// RunInviteTimes returns when each still-invited participant of a run was invited (or invited again), keyed by user id.
func (s *Service) RunInviteTimes(ctx context.Context, runID string) (map[string]time.Time, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT user_id, invited_at FROM run_participants WHERE run_id = ? AND status = 'invited'`, runID)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	out := map[string]time.Time{}
	for rows.Next() {
		var id string
		var at int64
		if err := rows.Scan(&id, &at); err != nil {
			return nil, internal(err)
		}
		out[id] = time.Unix(at, 0).UTC()
	}
	return out, internal2(rows.Err())
}

// RemoveRunInvite removes the open invitation of a user from a run that is not over (web interface). Only a
// participant who is still "invited" can be removed. The run gets a server event.
func (s *Service) RemoveRunInvite(ctx context.Context, actorID, runID, userID string) (err error) {
	defer s.publishOK(&err, TopicRuns)
	return s.adminInviteChange(ctx, actorID, runID, userID, "run.invite_removed", false, func(ctx context.Context, tx dbq, now time.Time) error {
		_, err := tx.ExecContext(ctx, `DELETE FROM run_participants WHERE run_id = ? AND user_id = ? AND status = 'invited'`, runID, userID)
		return internal2(err)
	})
}

// ResendRunInvite marks the invitation of a user as sent again (new invited_at, server event, push to the run's
// participants and the invited user's app). Only a participant who is still "invited" qualifies.
func (s *Service) ResendRunInvite(ctx context.Context, actorID, runID, userID string) (err error) {
	defer s.publishOK(&err, TopicRuns)
	return s.adminInviteChange(ctx, actorID, runID, userID, "run.invite_resent", true, func(ctx context.Context, tx dbq, now time.Time) error {
		_, err := tx.ExecContext(ctx, `UPDATE run_participants SET invited_at = ? WHERE run_id = ? AND user_id = ? AND status = 'invited'`, now.Unix(), runID, userID)
		return internal2(err)
	})
}

func (s *Service) adminInviteChange(ctx context.Context, actorID, runID, userID, eventType string, notify bool,
	apply func(ctx context.Context, tx dbq, now time.Time) error) error {
	actorName := userDisplayName(ctx, s.db, actorID)
	s.runs.mu.Lock()
	defer s.runs.mu.Unlock()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	run, err := loadRun(ctx, tx, runID, true)
	if err != nil {
		return err
	}
	if run.Finished() {
		return conflict("The run is over")
	}
	p, ok := run.Participant(userID)
	if !ok {
		return ErrNotFound
	}
	if p.Status != ParticipantInvited {
		return conflict("The user is not waiting for an invitation")
	}
	now := s.now().UTC()
	if err := apply(ctx, tx, now); err != nil {
		return err
	}
	if _, err := tx.ExecContext(ctx, `UPDATE runs SET updated_at = ? WHERE id = ?`, now.Unix(), runID); err != nil {
		return internal(err)
	}
	ev, err := insertEvent(ctx, tx, runID, hubEvent(eventType, map[string]any{
		"user_id": actorID, "user_name": actorName, "target_user_id": userID, "target_user_name": p.UserName}), now)
	if err != nil {
		return err
	}
	if err := tx.Commit(); err != nil {
		return internal(err)
	}
	run, err = loadRun(ctx, s.db, runID, true)
	if err != nil {
		return err
	}
	s.pushRunUpdated(run)
	if notify { // the invited user's app gets the resent invitation and its event
		s.pushRunEvents(ctx, runID, []RunEvent{ev}, userID)
	} else {
		s.pushRunEvents(ctx, runID, []RunEvent{ev})
	}
	return nil
}
