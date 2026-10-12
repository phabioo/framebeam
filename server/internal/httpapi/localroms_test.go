package httpapi

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net/http/httptest"
	"strings"
	"testing"
)

// Local ROMs over HTTP (feature local_roms_v1); every request is validated against the OpenAPI spec.

type apiGame struct {
	ID     string `json:"id"`
	OnHub  bool   `json:"on_hub"`
	GameV  *int   `json:"game_version"`
	Rom    apiRom `json:"rom"`
	Roms   []apiRom
	System string `json:"system"`
}

type apiRom struct {
	Sha256 string `json:"sha256"`
	Stored bool   `json:"stored"`
}

func apiDump(tail string) []byte {
	b := make([]byte, 0x200)
	copy(b[0x0C:], "ABCD")
	b[0x1E] = 4
	return append(b, tail...)
}

func gamesOf(t *testing.T, s *sessEnv, d dev) map[string]apiGame {
	t.Helper()
	rec := s.do("GET", "/api/v1/games", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	out := map[string]apiGame{}
	for _, g := range decode[struct{ Games []apiGame }](t, rec).Games {
		out[g.ID] = g
	}
	return out
}

func TestLocalROMsOverHTTP(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	adminDev := s.device(s.admin.ID, "Admin PC")
	annaDev := s.device(s.anna.ID, "Anna PC")
	bobDev := s.device(s.bob.ID, "Bob PC")

	rec := s.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: annaDev.tok})
	wantStatus(t, rec, 200, "")
	if f := decode[struct{ Features []string }](t, rec).Features; !contains(f, "local_roms_v1") {
		t.Fatalf("features %v", f)
	}

	local := apiDump("annas-dump")
	sum := sha256.Sum256(local)
	sha := hex.EncodeToString(sum[:])
	body := map[string]any{"sha256": sha, "size": len(local), "filename": "abcd.nds", "game_code": "ABCD", "game_version": 4}
	rec = s.do("POST", "/api/v1/games/register", body, opt{token: annaDev.tok})
	wantStatus(t, rec, 201, "")
	g := decode[apiGame](t, rec)
	if g.OnHub || g.Rom.Stored || len(g.Roms) != 1 || g.Roms[0].Sha256 != sha || g.GameV == nil || *g.GameV != 4 {
		t.Fatalf("%+v", g)
	}
	// Registering again (same user, same file) is 200 with the same game.
	rec = s.do("POST", "/api/v1/games/register", body, opt{token: annaDev.tok})
	wantStatus(t, rec, 200, "")
	if decode[apiGame](t, rec).ID != g.ID {
		t.Fatal("same game expected")
	}
	wantStatus(t, s.do("POST", "/api/v1/games/register", map[string]any{"sha256": "bad", "size": 1, "filename": "a.nds"}, opt{token: annaDev.tok, raw: true}), 400, "bad_request")

	// Visibility: Anna sees the game, Bob and the admin do not (list, get); the ROM is not downloadable.
	if _, ok := gamesOf(t, s, annaDev)[g.ID]; !ok {
		t.Fatal("anna must see her local game")
	}
	for _, d := range []dev{bobDev, adminDev} {
		if _, ok := gamesOf(t, s, d)[g.ID]; ok {
			t.Fatal("local game must be invisible to others")
		}
		wantStatus(t, s.do("GET", "/api/v1/games/"+g.ID, nil, opt{token: d.tok}), 404, "not_found")
	}
	wantStatus(t, s.do("GET", "/api/v1/games/"+g.ID, nil, opt{token: annaDev.tok}), 200, "")
	wantStatus(t, s.do("GET", "/api/v1/roms/"+sha, nil, opt{token: annaDev.tok}), 404, "not_found")

	// Uploads need the upload permission.
	cu := map[string]any{"filename": "abcd.nds", "size": len(local), "sha256": sha}
	wantStatus(t, s.do("POST", "/api/v1/uploads", cu, opt{token: annaDev.tok}), 403, "uploads_disabled")
	if err := s.svc.SetAllowUserUploads(ctx, true); err != nil {
		t.Fatal(err)
	}

	// Anna uploads her own file in chunks: the game_id stays and the game becomes visible to everyone.
	rec = s.do("POST", "/api/v1/uploads", cu, opt{token: annaDev.tok})
	wantStatus(t, rec, 201, "")
	up := decode[struct {
		UploadID  string `json:"upload_id"`
		Offset    int64
		ChunkSize int64 `json:"chunk_size"`
	}](t, rec)
	if up.Offset != 0 || up.ChunkSize != 64<<20 {
		t.Fatalf("%+v", up)
	}
	put := func(d dev, from, to int, rangeHdr string) *httptest.ResponseRecorder {
		if rangeHdr == "" {
			rangeHdr = fmt.Sprintf("bytes %d-%d/%d", from, to-1, len(local))
		}
		return s.do("PUT", "/api/v1/uploads/"+up.UploadID, nil, opt{token: d.tok, data: local[from:to], ctype: "application/octet-stream",
			header: map[string]string{"Content-Range": rangeHdr}})
	}
	wantStatus(t, put(annaDev, 100, 200, ""), 409, "conflict") // not at the offset
	wantStatus(t, put(bobDev, 0, 100, ""), 404, "not_found")   // another user's session (Bob may not upload: 403 first)
	rec = put(annaDev, 0, 300, "")
	wantStatus(t, rec, 200, "")
	if r := decode[struct {
		Offset int64
		Game   *apiGame
	}](t, rec); r.Offset != 300 || r.Game != nil {
		t.Fatalf("%+v", r)
	}
	// Resume: creating again returns the same session at the stored offset; GET shows it too.
	rec = s.do("POST", "/api/v1/uploads", cu, opt{token: annaDev.tok})
	wantStatus(t, rec, 201, "")
	if r := decode[struct {
		UploadID string `json:"upload_id"`
		Offset   int64
	}](t, rec); r.UploadID != up.UploadID || r.Offset != 300 {
		t.Fatalf("%+v", r)
	}
	rec = s.do("GET", "/api/v1/uploads/"+up.UploadID, nil, opt{token: annaDev.tok})
	wantStatus(t, rec, 200, "")
	if r := decode[struct{ Offset, Size int64 }](t, rec); r.Offset != 300 || r.Size != int64(len(local)) {
		t.Fatalf("%+v", r)
	}
	wantStatus(t, put(annaDev, 300, len(local), "bytes 300-9999999/99"), 400, "bad_request")
	rec = put(annaDev, 300, len(local), "")
	wantStatus(t, rec, 200, "")
	done := decode[struct {
		Offset int64
		Game   *apiGame
	}](t, rec)
	if done.Game == nil || done.Game.ID != g.ID || !done.Game.OnHub || !done.Game.Rom.Stored || done.Game.Rom.Sha256 != sha {
		t.Fatalf("finish: %+v", done)
	}
	wantStatus(t, s.do("GET", "/api/v1/roms/"+sha, nil, opt{token: bobDev.tok}), 200, "")
	for _, d := range []dev{bobDev, adminDev} {
		if _, ok := gamesOf(t, s, d)[g.ID]; !ok {
			t.Fatal("a game with a stored file is visible to everyone")
		}
	}
	// The same file again: 409 with the existing game (DuplicateGameError).
	rec = s.do("POST", "/api/v1/uploads", cu, opt{token: annaDev.tok})
	wantStatus(t, rec, 409, "conflict")
	if decode[struct {
		Existing string `json:"existing_game_id"`
	}](t, rec).Existing != g.ID {
		t.Fatalf("%s", rec.Body.String())
	}
	wantStatus(t, s.do("GET", "/api/v1/uploads/"+up.UploadID, nil, opt{token: annaDev.tok}), 404, "not_found")

	// Abort and remove a registration.
	rec = s.do("POST", "/api/v1/uploads", map[string]any{"filename": "x.nds", "size": 10}, opt{token: annaDev.tok})
	wantStatus(t, rec, 201, "")
	id2 := decode[struct {
		UploadID string `json:"upload_id"`
	}](t, rec).UploadID
	wantStatus(t, s.do("DELETE", "/api/v1/uploads/"+id2, nil, opt{token: annaDev.tok}), 204, "")
	wantStatus(t, s.do("DELETE", "/api/v1/uploads/"+id2, nil, opt{token: annaDev.tok}), 404, "not_found")
	wantStatus(t, s.do("DELETE", "/api/v1/games/"+g.ID+"/registration", nil, opt{token: annaDev.tok}), 204, "")
	wantStatus(t, s.do("DELETE", "/api/v1/games/00000000-0000-4000-8000-000000000000/registration", nil, opt{token: annaDev.tok}), 404, "not_found")
}

func contains(l []string, v string) bool {
	for _, x := range l {
		if x == v {
			return true
		}
	}
	return false
}

// A gba and a gb game with an identity pass the response validation against the OpenAPI spec (the new kinds).
func TestGameBoyIdentityOverHTTP(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	d := s.device(s.admin.ID, "Admin PC")
	gba := make([]byte, 0x100)
	copy(gba[0xAC:], "ABCE")
	gba[0xBC] = 2
	gb := make([]byte, 0x150)
	copy(gb[0x134:], "SYNTH")
	var sum byte
	for i := 0x134; i <= 0x14C; i++ {
		sum = sum - gb[i] - 1
	}
	gb[0x14D] = sum
	for name, rom := range map[string][]byte{"a.gba": gba, "b.gb": gb} {
		if _, err := s.svc.AddROM(ctx, bytes.NewReader(rom), name, "", "", s.admin.ID); err != nil {
			t.Fatal(err)
		}
	}
	rec := s.do("GET", "/api/v1/games", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	got := map[string]string{}
	for _, g := range decode[struct{ Games []map[string]any }](t, rec).Games {
		got[g["game_code_kind"].(string)] = g["game_code"].(string)
	}
	if got["gba-gamecode"] != "ABCE" || got["gb-title"] != "SYNTH" {
		t.Fatalf("%v", got)
	}
	// Registration with a Game Boy title from the Player.
	body := map[string]any{"sha256": strings.Repeat("ab", 32), "size": 100, "filename": "c.gbc", "game_code": "LONGTITLE12", "game_version": 1}
	wantStatus(t, s.do("POST", "/api/v1/games/register", body, opt{token: d.tok}), 201, "")
}
