package hub

import (
	"context"
	"database/sql"
	"errors"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/google/uuid"
)

// Personal library data of one user (0.19 package 4): play time per game, favorites and collections. Everything is
// per user on the server so it follows the person to every PC. Removing a game removes its favorites and collection
// entries (deleteGameRows); deleting a collection never deletes games.

// MaxCollectionNameLen is the longest collection name in characters.
const MaxCollectionNameLen = 40

// GamePersonal is what one user has with one game.
type GamePersonal struct {
	PlayStat
	Favorite      bool
	CollectionIDs []string
}

// Collection is a named list of games of one user.
type Collection struct {
	ID        string
	Name      string
	GameIDs   []string
	CreatedAt time.Time
}

// PersonalGames returns the personal data of userID per game ID: for one game when gameID is set, else for every
// game that has any. Three grouped queries, not one per game. Games without data have no entry.
func (s *Service) PersonalGames(ctx context.Context, userID, gameID string) (map[string]GamePersonal, error) {
	out := map[string]GamePersonal{}
	args := []any{userID}
	playWhere, favWhere, colWhere := ` AND user_id = ?2`, ``, ``
	if gameID != "" {
		args = append(args, gameID)
		playWhere += ` AND game_id = ?3`
		favWhere, colWhere = ` AND game_id = ?`, ` AND g.game_id = ?`
	}
	stats, err := s.playStats(ctx, `game_id`, playWhere, args...)
	if err != nil {
		return nil, err
	}
	for id, st := range stats {
		out[id] = GamePersonal{PlayStat: st}
	}
	rows, err := s.db.QueryContext(ctx, `SELECT game_id FROM favorites WHERE user_id = ?`+favWhere, args...)
	if err != nil {
		return nil, internal(err)
	}
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			rows.Close()
			return nil, internal(err)
		}
		p := out[id]
		p.Favorite = true
		out[id] = p
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return nil, internal(err)
	}
	rows, err = s.db.QueryContext(ctx, `SELECT g.game_id, c.id FROM collection_games g JOIN collections c ON c.id = g.collection_id
		WHERE c.user_id = ?`+colWhere+` ORDER BY c.created_at, c.rowid`, args...)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	for rows.Next() {
		var id, cid string
		if err := rows.Scan(&id, &cid); err != nil {
			return nil, internal(err)
		}
		p := out[id]
		p.CollectionIDs = append(p.CollectionIDs, cid)
		out[id] = p
	}
	return out, rows.Err()
}

// SetFavorite marks or unmarks a game as favorite of the user (idempotent). Marking needs a game the user can
// see (ErrNotFound otherwise); unmarking needs only an existing game.
func (s *Service) SetFavorite(ctx context.Context, userID, gameID string, on bool) error {
	if err := s.checkGameForPersonal(ctx, userID, gameID, on); err != nil {
		return err
	}
	var err error
	if on {
		_, err = s.db.ExecContext(ctx, `INSERT OR IGNORE INTO favorites(user_id, game_id, created_at) VALUES (?, ?, ?)`, userID, gameID, s.now().Unix())
	} else {
		_, err = s.db.ExecContext(ctx, `DELETE FROM favorites WHERE user_id = ? AND game_id = ?`, userID, gameID)
	}
	if err != nil {
		return internal(err)
	}
	return nil
}

// checkGameForPersonal: adding needs a game visible to the user, removing only an existing game.
func (s *Service) checkGameForPersonal(ctx context.Context, userID, gameID string, add bool) error {
	var err error
	if add {
		_, err = s.GetGameFor(ctx, userID, gameID)
	} else {
		var n int
		if err = s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM games WHERE id = ?`, gameID).Scan(&n); err != nil {
			return internal(err)
		}
		if n == 0 {
			return ErrNotFound
		}
	}
	return err
}

// cleanCollectionName trims the name and checks it: 1 to MaxCollectionNameLen characters, no control characters.
func cleanCollectionName(name string) (string, error) {
	name = strings.TrimSpace(name)
	n := utf8.RuneCountInString(name)
	if n < 1 || n > MaxCollectionNameLen || !utf8.ValidString(name) {
		return "", badRequest("The name must be 1 to %d characters long", MaxCollectionNameLen)
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return "", badRequest("The name must not contain control characters")
		}
	}
	return name, nil
}

var errCollectionNameTaken = conflict("You already have a collection with this name")

// ListCollections returns the user's collections (oldest first) with their games (in the order they were added).
func (s *Service) ListCollections(ctx context.Context, userID string) ([]Collection, error) {
	return s.loadCollections(ctx, userID, "")
}

func (s *Service) loadCollections(ctx context.Context, userID, id string) ([]Collection, error) {
	q, args := `SELECT id, name, created_at FROM collections WHERE user_id = ?`, []any{userID}
	if id != "" {
		q, args = q+` AND id = ?`, append(args, id)
	}
	rows, err := s.db.QueryContext(ctx, q+` ORDER BY created_at, rowid`, args...)
	if err != nil {
		return nil, internal(err)
	}
	out := []Collection{}
	idx := map[string]int{}
	for rows.Next() {
		var c Collection
		var created int64
		if err := rows.Scan(&c.ID, &c.Name, &created); err != nil {
			rows.Close()
			return nil, internal(err)
		}
		c.CreatedAt, c.GameIDs = unixTime(created), []string{}
		idx[c.ID] = len(out)
		out = append(out, c)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return nil, internal(err)
	}
	rows, err = s.db.QueryContext(ctx, `SELECT g.collection_id, g.game_id FROM collection_games g JOIN collections c ON c.id = g.collection_id
		WHERE c.user_id = ? ORDER BY g.added_at, g.rowid`, userID)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	for rows.Next() {
		var cid, gid string
		if err := rows.Scan(&cid, &gid); err != nil {
			return nil, internal(err)
		}
		if i, ok := idx[cid]; ok {
			out[i].GameIDs = append(out[i].GameIDs, gid)
		}
	}
	return out, rows.Err()
}

// GetCollection returns one collection of the user (ErrNotFound for another user's or an unknown one).
func (s *Service) GetCollection(ctx context.Context, userID, id string) (Collection, error) {
	cs, err := s.loadCollections(ctx, userID, id)
	if err != nil {
		return Collection{}, err
	}
	if len(cs) == 0 {
		return Collection{}, ErrNotFound
	}
	return cs[0], nil
}

// CreateCollection creates a collection with optional games. Unknown or invisible games are a bad request;
// a name the user already uses (case-insensitive) is a conflict.
func (s *Service) CreateCollection(ctx context.Context, userID, name string, gameIDs []string) (Collection, error) {
	name, err := cleanCollectionName(name)
	if err != nil {
		return Collection{}, err
	}
	seen := map[string]bool{}
	var games []string
	for _, g := range gameIDs {
		if seen[g] {
			continue
		}
		seen[g] = true
		if _, err := s.GetGameFor(ctx, userID, g); errors.Is(err, ErrNotFound) {
			return Collection{}, badRequest("game_ids contains an unknown game")
		} else if err != nil {
			return Collection{}, err
		}
		games = append(games, g)
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Collection{}, internal(err)
	}
	defer tx.Rollback()
	id, now := uuid.NewString(), s.now().Unix()
	if _, err := tx.ExecContext(ctx, `INSERT INTO collections(id, user_id, name, created_at) VALUES (?, ?, ?, ?)`, id, userID, name, now); err != nil {
		if isUnique(err) {
			return Collection{}, errCollectionNameTaken
		}
		return Collection{}, internal(err)
	}
	for _, g := range games {
		if _, err := tx.ExecContext(ctx, `INSERT INTO collection_games(collection_id, game_id, added_at) VALUES (?, ?, ?)`, id, g, now); err != nil {
			return Collection{}, internal(err)
		}
	}
	if err := tx.Commit(); err != nil {
		return Collection{}, internal(err)
	}
	return s.GetCollection(ctx, userID, id)
}

// RenameCollection changes the name (same rules as on create; renaming to its own name in another case is fine).
func (s *Service) RenameCollection(ctx context.Context, userID, id, name string) (Collection, error) {
	name, err := cleanCollectionName(name)
	if err != nil {
		return Collection{}, err
	}
	res, err := s.db.ExecContext(ctx, `UPDATE collections SET name = ? WHERE id = ? AND user_id = ?`, name, id, userID)
	if err != nil {
		if isUnique(err) {
			return Collection{}, errCollectionNameTaken
		}
		return Collection{}, internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return Collection{}, ErrNotFound
	}
	return s.GetCollection(ctx, userID, id)
}

// DeleteCollection removes the collection and its entries; the games stay.
func (s *Service) DeleteCollection(ctx context.Context, userID, id string) error {
	res, err := s.db.ExecContext(ctx, `DELETE FROM collections WHERE id = ? AND user_id = ?`, id, userID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return nil
}

// SetCollectionGame adds or removes a game in a collection (idempotent). ErrNotFound for another user's or an
// unknown collection, and for a game the user cannot see when adding or that does not exist when removing.
func (s *Service) SetCollectionGame(ctx context.Context, userID, id, gameID string, on bool) error {
	var one int
	err := s.db.QueryRowContext(ctx, `SELECT 1 FROM collections WHERE id = ? AND user_id = ?`, id, userID).Scan(&one)
	if errors.Is(err, sql.ErrNoRows) {
		return ErrNotFound
	}
	if err != nil {
		return internal(err)
	}
	if err := s.checkGameForPersonal(ctx, userID, gameID, on); err != nil {
		return err
	}
	if on {
		_, err = s.db.ExecContext(ctx, `INSERT OR IGNORE INTO collection_games(collection_id, game_id, added_at) VALUES (?, ?, ?)`, id, gameID, s.now().Unix())
	} else {
		_, err = s.db.ExecContext(ctx, `DELETE FROM collection_games WHERE collection_id = ? AND game_id = ?`, id, gameID)
	}
	if err != nil {
		return internal(err)
	}
	return nil
}
