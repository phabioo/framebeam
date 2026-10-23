package httpapi

import (
	"context"
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Personal library data over REST (0.19 package 4): play time, favorites, collections.

type personalT struct {
	PlaySeconds   int64    `json:"play_seconds"`
	Sessions      int      `json:"sessions"`
	LastPlayedAt  *string  `json:"last_played_at"`
	Favorite      bool     `json:"favorite"`
	CollectionIDs []string `json:"collection_ids"`
}

type gameT struct {
	ID       string     `json:"id"`
	Personal *personalT `json:"personal"`
}

type collectionT struct {
	ID        string   `json:"id"`
	Name      string   `json:"name"`
	GameIDs   []string `json:"game_ids"`
	CreatedAt string   `json:"created_at"`
}

// present sends presence for the device of d over a hub client (the WSS handler does the same).
func (s *sessEnv) present(d dev, state, game, run string) *hub.Client {
	s.t.Helper()
	ctx := context.Background()
	dv, err := s.svc.GetDevice(ctx, d.id)
	if err != nil {
		s.t.Fatal(err)
	}
	u, err := s.svc.GetUser(ctx, d.user)
	if err != nil {
		s.t.Fatal(err)
	}
	cl := s.svc.NewClient(hub.Principal{Device: dv, User: u})
	if err := cl.Handle([]byte(fmt.Sprintf(`{"type":"hello","payload":{"protocol_version":1,"device_id":%q}}`, d.id))); err != nil {
		s.t.Fatal(err)
	}
	s.send(cl, state, game, run)
	return cl
}

func (s *sessEnv) send(cl *hub.Client, state, game, run string) {
	s.t.Helper()
	p := fmt.Sprintf(`{"state":%q`, state)
	if game != "" {
		p += fmt.Sprintf(`,"game_id":%q`, game)
	}
	if run != "" {
		p += fmt.Sprintf(`,"run_id":%q`, run)
	}
	if err := cl.Handle([]byte(`{"type":"presence_update","payload":` + p + `}}`)); err != nil {
		s.t.Fatal(err)
	}
}

func TestFavoritesAndCollectionsAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	anna, bob := s.device(s.anna.ID, "Annas PC"), s.device(s.bob.ID, "Bobs PC")
	gid := s.game.ID
	const unknown = "00000000-0000-4000-8000-000000000000"

	wantStatus(t, s.do("GET", "/api/v1/collections", nil, opt{}), 401, "")
	wantStatus(t, s.do("PUT", "/api/v1/games/"+gid+"/favorite", nil, opt{}), 401, "")

	// Every game of the list carries personal, with zeros and empty lists when there is nothing.
	list := decode[struct{ Games []gameT }](t, s.api(anna, "GET", "/api/v1/games", nil)).Games
	if len(list) != 1 || list[0].Personal == nil {
		t.Fatalf("%+v", list)
	}
	if p := list[0].Personal; p.PlaySeconds != 0 || p.Sessions != 0 || p.LastPlayedAt != nil || p.Favorite || p.CollectionIDs == nil || len(p.CollectionIDs) != 0 {
		t.Fatalf("empty personal: %+v", p)
	}

	// Favorites: idempotent, per user, 404 for unknown games.
	for i := 0; i < 2; i++ {
		wantStatus(t, s.api(anna, "PUT", "/api/v1/games/"+gid+"/favorite", nil), 204, "")
	}
	wantStatus(t, s.api(anna, "PUT", "/api/v1/games/"+unknown+"/favorite", nil), 404, "not_found")
	wantStatus(t, s.api(anna, "DELETE", "/api/v1/games/"+unknown+"/favorite", nil), 404, "not_found")
	one := decode[gameT](t, s.api(anna, "GET", "/api/v1/games/"+gid, nil))
	if one.Personal == nil || !one.Personal.Favorite {
		t.Fatalf("anna: %+v", one.Personal)
	}
	if g := decode[gameT](t, s.api(bob, "GET", "/api/v1/games/"+gid, nil)); g.Personal.Favorite {
		t.Fatal("favorite leaked to another user")
	}
	for i := 0; i < 2; i++ {
		wantStatus(t, s.api(anna, "DELETE", "/api/v1/games/"+gid+"/favorite", nil), 204, "")
	}
	if g := decode[gameT](t, s.api(anna, "GET", "/api/v1/games/"+gid, nil)); g.Personal.Favorite {
		t.Fatal("favorite not removed")
	}

	// Collections: create (name trimmed), list, rename, name rules.
	rec := s.api(anna, "POST", "/api/v1/collections", map[string]any{"name": "  Retro  ", "game_ids": []string{gid}})
	wantStatus(t, rec, 201, "")
	c := decode[collectionT](t, rec)
	if c.Name != "Retro" || len(c.GameIDs) != 1 || c.GameIDs[0] != gid || c.ID == "" || c.CreatedAt == "" {
		t.Fatalf("%+v", c)
	}
	for _, name := range []string{"", "   ", strings.Repeat("x", 41)} {
		wantStatus(t, s.do("POST", "/api/v1/collections", map[string]any{"name": name}, opt{token: anna.tok, raw: true}), 400, "bad_request")
	}
	wantStatus(t, s.api(anna, "POST", "/api/v1/collections", map[string]any{"name": "RETRO"}), 409, "conflict")
	wantStatus(t, s.api(anna, "POST", "/api/v1/collections", map[string]any{"name": "X", "game_ids": []string{unknown}}), 400, "bad_request")
	wantStatus(t, s.api(bob, "POST", "/api/v1/collections", map[string]any{"name": "Retro"}), 201, "")
	other := decode[struct{ Collections []collectionT }](t, s.api(bob, "GET", "/api/v1/collections", nil)).Collections
	if len(other) != 1 || other[0].ID == c.ID || len(other[0].GameIDs) != 0 {
		t.Fatalf("bob's collections: %+v", other)
	}
	second := decode[collectionT](t, s.api(anna, "POST", "/api/v1/collections", map[string]any{"name": "Second"}))
	wantStatus(t, s.api(anna, "PATCH", "/api/v1/collections/"+second.ID, map[string]any{"name": "retro"}), 409, "conflict")
	rec = s.api(anna, "PATCH", "/api/v1/collections/"+c.ID, map[string]any{"name": "retro"}) // own name, other case
	wantStatus(t, rec, 200, "")
	if got := decode[collectionT](t, rec); got.Name != "retro" || got.ID != c.ID {
		t.Fatalf("%+v", got)
	}

	// Isolation: another user's collection is 404 for every operation.
	wantStatus(t, s.api(bob, "PATCH", "/api/v1/collections/"+c.ID, map[string]any{"name": "Mine"}), 404, "not_found")
	wantStatus(t, s.api(bob, "DELETE", "/api/v1/collections/"+c.ID, nil), 404, "not_found")
	wantStatus(t, s.api(bob, "PUT", "/api/v1/collections/"+c.ID+"/games/"+gid, nil), 404, "not_found")
	wantStatus(t, s.api(bob, "DELETE", "/api/v1/collections/"+c.ID+"/games/"+gid, nil), 404, "not_found")

	// Membership: idempotent, game_ids and collection_ids follow.
	for i := 0; i < 2; i++ {
		wantStatus(t, s.api(anna, "PUT", "/api/v1/collections/"+second.ID+"/games/"+gid, nil), 204, "")
	}
	wantStatus(t, s.api(anna, "PUT", "/api/v1/collections/"+second.ID+"/games/"+unknown, nil), 404, "not_found")
	wantStatus(t, s.api(anna, "PUT", "/api/v1/collections/"+unknown+"/games/"+gid, nil), 404, "not_found")
	g := decode[gameT](t, s.api(anna, "GET", "/api/v1/games/"+gid, nil))
	if len(g.Personal.CollectionIDs) != 2 || g.Personal.CollectionIDs[0] != c.ID || g.Personal.CollectionIDs[1] != second.ID {
		t.Fatalf("collection_ids: %v", g.Personal.CollectionIDs)
	}
	for i := 0; i < 2; i++ {
		wantStatus(t, s.api(anna, "DELETE", "/api/v1/collections/"+second.ID+"/games/"+gid, nil), 204, "")
	}

	// Deleting a collection never deletes games.
	wantStatus(t, s.api(anna, "DELETE", "/api/v1/collections/"+c.ID, nil), 204, "")
	wantStatus(t, s.api(anna, "DELETE", "/api/v1/collections/"+c.ID, nil), 404, "not_found")
	wantStatus(t, s.api(anna, "GET", "/api/v1/games/"+gid, nil), 200, "")
	if cs := decode[struct{ Collections []collectionT }](t, s.api(anna, "GET", "/api/v1/collections", nil)).Collections; len(cs) != 1 || cs[0].ID != second.ID {
		t.Fatalf("%+v", cs)
	}
}

func TestPlayTimeInGamesAccountAdminAndRunsAPI(t *testing.T) {
	r := newRunEnv(t)
	anna, bob := r.device(r.anna.ID, "Annas PC"), r.device(r.bob.ID, "Bobs PC")
	adm := r.device(r.admin.ID, "Admin PC")
	gid := r.game.ID
	run := r.create(anna, "Run", r.bob.ID)
	r.mustJoin(bob, run.RunID)

	// Anna plays 90 s inside the run, then 30 s outside. Bob plays 40 s in the run.
	cl := r.present(anna, "in_game", gid, run.RunID)
	bcl := r.present(bob, "in_game", gid, run.RunID)
	r.clk.Advance(40 * time.Second)
	r.send(bcl, "online", "", "")
	r.clk.Advance(50 * time.Second)
	r.send(cl, "in_game", gid, "")
	r.clk.Advance(30 * time.Second)

	// Open plays count up to now.
	g := decode[gameT](t, r.api(anna, "GET", "/api/v1/games/"+gid, nil))
	if p := g.Personal; p.PlaySeconds != 120 || p.Sessions != 2 || p.LastPlayedAt == nil || p.Favorite {
		t.Fatalf("game personal: %+v", p)
	}
	list := decode[struct{ Games []gameT }](t, r.api(bob, "GET", "/api/v1/games", nil)).Games
	if len(list) != 1 || list[0].Personal.PlaySeconds != 40 || list[0].Personal.Sessions != 1 {
		t.Fatalf("bob's list: %+v", list[0].Personal)
	}

	acc := decode[struct {
		PlaySeconds  int64   `json:"play_seconds"`
		Sessions     int     `json:"sessions"`
		LastPlayedAt *string `json:"last_played_at"`
	}](t, r.api(anna, "GET", "/api/v1/account", nil))
	if acc.PlaySeconds != 120 || acc.Sessions != 2 || acc.LastPlayedAt == nil {
		t.Fatalf("account: %+v", acc)
	}
	carolAcc := decode[struct {
		PlaySeconds  int64   `json:"play_seconds"`
		LastPlayedAt *string `json:"last_played_at"`
	}](t, r.api(adm, "GET", "/api/v1/account", nil))
	if carolAcc.PlaySeconds != 0 || carolAcc.LastPlayedAt != nil {
		t.Fatalf("account without plays: %+v", carolAcc)
	}

	r.confirm(adm.tok)
	users := decode[struct {
		Users []struct {
			ID           string  `json:"id"`
			PlaySeconds  int64   `json:"play_seconds"`
			Sessions     int     `json:"sessions"`
			LastPlayedAt *string `json:"last_played_at"`
		}
	}](t, r.api(adm, "GET", "/api/v1/admin/users", nil)).Users
	seen := map[string]int64{}
	for _, u := range users {
		seen[u.ID] = u.PlaySeconds
		if u.ID == r.anna.ID && (u.Sessions != 2 || u.LastPlayedAt == nil) {
			t.Fatalf("anna: %+v", u)
		}
		if u.ID == r.admin.ID && (u.PlaySeconds != 0 || u.LastPlayedAt != nil) {
			t.Fatalf("admin: %+v", u)
		}
	}
	if seen[r.anna.ID] != 120 || seen[r.bob.ID] != 40 {
		t.Fatalf("admin users: %v", seen)
	}
	rec := r.api(adm, "PATCH", "/api/v1/admin/users/"+r.anna.ID, map[string]any{"display_name": "Anna"})
	wantStatus(t, rec, 200, "")
	if u := decode[struct {
		PlaySeconds int64 `json:"play_seconds"`
	}](t, rec); u.PlaySeconds != 120 {
		t.Fatalf("admin user detail: %+v", u)
	}

	// Runs: per member and in total (only the 90 s + 40 s inside the run).
	type runT struct {
		RunID        string `json:"run_id"`
		PlaySeconds  int64  `json:"play_seconds"`
		Participants []struct {
			UserID       string  `json:"user_id"`
			PlaySeconds  int64   `json:"play_seconds"`
			LastPlayedAt *string `json:"last_played_at"`
		}
	}
	check := func(label string, rn runT) {
		t.Helper()
		if rn.PlaySeconds != 130 || len(rn.Participants) != 2 {
			t.Fatalf("%s: %+v", label, rn)
		}
		for _, p := range rn.Participants {
			want := map[string]int64{r.anna.ID: 90, r.bob.ID: 40}[p.UserID]
			if p.PlaySeconds != want || p.LastPlayedAt == nil {
				t.Fatalf("%s: %+v", label, p)
			}
		}
	}
	check("get", decode[runT](t, r.api(anna, "GET", "/api/v1/runs/"+run.RunID, nil)))
	runs := decode[struct{ Runs []runT }](t, r.api(bob, "GET", "/api/v1/runs", nil)).Runs
	if len(runs) != 1 {
		t.Fatalf("%+v", runs)
	}
	check("list", runs[0])

	// A user that is not a member cannot make a play count for the run.
	r.present(r.device(r.carol.ID, "Carols PC"), "in_game", gid, run.RunID)
	r.clk.Advance(time.Minute)
	check("after outsider", decode[runT](t, r.api(anna, "GET", "/api/v1/runs/"+run.RunID, nil)))
}
