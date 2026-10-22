package httpapi

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

type adminSysT struct {
	ID                  string
	DefaultCoreID       *string `json:"default_core_id"`
	FirmwareMode        string  `json:"firmware_mode"`
	AutoUpdate          string  `json:"auto_update"`
	AutoUpdateEffective bool    `json:"auto_update_effective"`
	Cores               []struct {
		CoreID              string `json:"core_id"`
		Default             bool
		AutoUpdate          string `json:"auto_update"`
		AutoUpdateEffective bool   `json:"auto_update_effective"`
		UpdateAvailable     bool   `json:"update_available"`
		Origin              string
	}
	AvailableCores []struct {
		CoreID string `json:"core_id"`
	} `json:"available_cores"`
	Firmware []struct {
		ID           string
		Present      bool
		Required     bool
		Size         *int64
		SHA256       *string `json:"sha256"`
		PinnedSHA256 *string `json:"pinned_sha256"`
		State        string
	}
}

func (a adminSysT) core(id string) (found bool, def bool, mode string) {
	for _, c := range a.Cores {
		if c.CoreID == id {
			return true, c.Default, c.AutoUpdate
		}
	}
	return
}

func TestAdminSystemsRequireAdmin(t *testing.T) {
	s := newSessEnv(t, nil)
	member := s.device(s.anna.ID, "Annas PC")
	adminAuthzTable(t, s, member, [][2]string{
		{"GET", "/api/v1/admin/systems"}, {"PATCH", "/api/v1/admin/systems/nds"}, {"PATCH", "/api/v1/admin/systems/nds/cores/desmume"},
		{"DELETE", "/api/v1/admin/systems/nds/cores/desmume"}, {"POST", "/api/v1/admin/systems/nds/cores/desmume/install"},
		{"POST", "/api/v1/admin/systems/nds/cores/desmume/update"},
		{"GET", "/api/v1/admin/systems/nds/firmware/bios7"}, {"PUT", "/api/v1/admin/systems/nds/firmware/bios7"},
		{"DELETE", "/api/v1/admin/systems/nds/firmware/bios7"}, {"POST", "/api/v1/admin/systems/nds/firmware/bios7/pin"},
		{"POST", "/api/v1/admin/cores/sync"}, {"PATCH", "/api/v1/admin/cores/settings"},
	})
}

func TestAdminCoresAPI(t *testing.T) {
	s, _ := newBuildbotEnv(t)
	ctx := context.Background()
	tok := opt{token: s.device(s.admin.ID, "Admin PC").tok}
	get := func() adminSysT {
		rec := s.do("GET", "/api/v1/admin/systems", nil, tok)
		wantStatus(t, rec, 200, "")
		for _, x := range decode[struct{ Systems []adminSysT }](t, rec).Systems {
			if x.ID == "nds" {
				return x
			}
		}
		t.Fatal("no nds system")
		return adminSysT{}
	}

	// Nothing installed: both buildbot cores are available, the catalog state is shown.
	nds := get()
	if len(nds.Cores) != 0 || len(nds.AvailableCores) != 2 || nds.DefaultCoreID != nil || nds.AutoUpdate != "default" || nds.FirmwareMode != "builtin" {
		t.Fatalf("%+v", nds)
	}
	src := decode[struct {
		CoreSource struct {
			BuildbotURL string `json:"buildbot_url"`
			Cores       int
			AutoUpdate  bool `json:"auto_update"`
		} `json:"core_source"`
	}](t, s.do("GET", "/api/v1/admin/systems", nil, tok)).CoreSource
	if src.BuildbotURL == "" || src.Cores != 2 {
		t.Fatalf("%+v", src)
	}

	// Install: the answer is the updated system.
	rec := s.do("POST", "/api/v1/admin/systems/nds/cores/desmume/install", nil, tok)
	wantStatus(t, rec, 200, "")
	nds = decode[adminSysT](t, rec)
	if found, def, mode := nds.core("desmume"); !found || !def || mode != "auto" || nds.DefaultCoreID == nil || *nds.DefaultCoreID != "desmume" || len(nds.AvailableCores) != 1 {
		t.Fatalf("%+v", nds)
	}
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); len(e.Cores) != 1 || e.CoreID != "desmume" {
		t.Fatalf("%+v", e)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/systems/nds/cores/nope/install", nil, tok), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/systems/nope/cores/desmume/install", nil, tok), 404, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/systems/nds/cores/desmume/update", nil, tok), 400, "") // already up to date

	// A second core, then the default moves.
	wantStatus(t, s.do("POST", "/api/v1/admin/systems/nds/cores/noods/install", nil, tok), 200, "")
	rec = s.do("PATCH", "/api/v1/admin/systems/nds", map[string]any{"default_core_id": "noods"}, tok)
	wantStatus(t, rec, 200, "")
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); e.CoreID != "noods" {
		t.Fatalf("default %q", e.CoreID)
	}
	if _, def, _ := decode[adminSysT](t, rec).core("noods"); !def {
		t.Fatal("noods is not the default in the answer")
	}
	if rec := s.do("PATCH", "/api/v1/admin/systems/nds", map[string]any{"default_core_id": "ghost"}, tok); rec.Code < 400 || rec.Code >= 500 {
		t.Fatalf("%d %s", rec.Code, rec.Body.String())
	}

	// Auto-update: system override, per-core pin, firmware mode, validated before anything is applied.
	rec = s.do("PATCH", "/api/v1/admin/systems/nds", map[string]any{"auto_update": "off", "firmware_mode": "native"}, tok)
	wantStatus(t, rec, 200, "")
	nds = decode[adminSysT](t, rec)
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); e.AutoUpdate != hub.SystemAutoOff || e.FirmwareMode != hub.FirmwareNative ||
		nds.AutoUpdate != "off" || nds.AutoUpdateEffective || nds.FirmwareMode != "native" {
		t.Fatalf("%+v %+v", e, nds)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/systems/nds", map[string]any{"auto_update": "on", "firmware_mode": "sideways"}, opt{token: tok.token, raw: true}), 400, "")
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); e.AutoUpdate != hub.SystemAutoOff {
		t.Fatalf("a refused request changed the system: %+v", e)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/systems/nds", map[string]any{"auto_update": "default"}, tok), 200, "")
	rec = s.do("PATCH", "/api/v1/admin/systems/nds/cores/desmume", map[string]any{"auto_update": "off"}, tok)
	wantStatus(t, rec, 200, "")
	if _, _, mode := decode[adminSysT](t, rec).core("desmume"); mode != "off" {
		t.Fatalf("mode %q", mode)
	}
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); e.AutoUpdate != hub.SystemAutoDefault {
		t.Fatalf("%+v", e)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/systems/nds/cores/desmume", map[string]any{"auto_update": "maybe"}, opt{token: tok.token, raw: true}), 400, "")
	wantStatus(t, s.do("PATCH", "/api/v1/admin/systems/nds/cores/ghost", map[string]any{"auto_update": "off"}, tok), 404, "")

	// Remove a core.
	rec = s.do("DELETE", "/api/v1/admin/systems/nds/cores/desmume", nil, tok)
	wantStatus(t, rec, 200, "")
	if found, _, _ := decode[adminSysT](t, rec).core("desmume"); found {
		t.Fatal("desmume still installed in the answer")
	}
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); len(e.Cores) != 1 {
		t.Fatalf("%+v", e.Cores)
	}
	wantStatus(t, s.do("DELETE", "/api/v1/admin/systems/nds/cores/desmume", nil, tok), 404, "")

	// Catalog refresh and the server default.
	wantStatus(t, s.do("POST", "/api/v1/admin/cores/sync", nil, tok), 202, "")
	rec = s.do("PATCH", "/api/v1/admin/cores/settings", map[string]any{"auto_update": false}, tok)
	wantStatus(t, rec, 200, "")
	if on, _ := s.svc.CoreAutoUpdate(ctx); on || decode[struct {
		AutoUpdate bool `json:"auto_update"`
	}](t, rec).AutoUpdate {
		t.Fatal("auto update still on")
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/cores/settings", map[string]any{"auto_update": true}, tok), 200, "")
	if on, _ := s.svc.CoreAutoUpdate(ctx); !on {
		t.Fatal("auto update not on")
	}
}

func TestAdminFirmwareAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	tok := opt{token: s.device(s.admin.ID, "Admin PC").tok}
	const base = "/api/v1/admin/systems/nds/firmware/bios7"
	state := func(rec adminSysT) (present bool, st string, pin *string) {
		for _, f := range rec.Firmware {
			if f.ID == "bios7" {
				return f.Present, f.State, f.PinnedSHA256
			}
		}
		t.Fatal("no bios7")
		return
	}

	// Dummy bytes of the right size; a wrong size is refused.
	b7 := bytes.Repeat([]byte{7}, 16384)
	put := func(data []byte) adminSysT {
		rec := s.do("PUT", base, nil, opt{token: tok.token, data: data, ctype: "application/octet-stream"})
		wantStatus(t, rec, 200, "")
		return decode[adminSysT](t, rec)
	}
	wantStatus(t, s.do("PUT", base, nil, opt{token: tok.token, data: []byte("too short"), ctype: "application/octet-stream"}), 400, "")
	wantStatus(t, s.do("PUT", "/api/v1/admin/systems/nds/firmware/nope", nil, opt{token: tok.token, data: b7, ctype: "application/octet-stream"}), 404, "")
	wantStatus(t, s.do("PUT", base, nil, opt{token: tok.token, data: bytes.Repeat([]byte{1}, 2<<20), ctype: "application/octet-stream"}), 400, "")
	wantStatus(t, s.do("GET", base, nil, tok), 404, "")
	if present, st, _ := state(put(b7)); !present || st != "optional" && st != "valid" {
		t.Fatalf("present %v state %q", present, st)
	}
	f, def, err := s.svc.OpenFirmware(ctx, "nds", "bios7")
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
	if def.Size != 16384 {
		t.Fatalf("%+v", def)
	}

	// Download returns the bytes.
	rec := s.do("GET", base, nil, tok)
	wantStatus(t, rec, 200, "")
	if !bytes.Equal(rec.Body.Bytes(), b7) || rec.Header().Get("Content-Type") != "application/octet-stream" {
		t.Fatalf("download: %d bytes, type %q", rec.Body.Len(), rec.Header().Get("Content-Type"))
	}

	// Pin: the right hash is valid, another one is a mismatch, empty clears it, junk is refused.
	sum := sha256.Sum256(b7)
	want := hex.EncodeToString(sum[:])
	pin := func(sha string) adminSysT {
		rec := s.do("POST", base+"/pin", map[string]any{"sha256": sha}, tok)
		wantStatus(t, rec, 200, "")
		return decode[adminSysT](t, rec)
	}
	if _, st, p := state(pin(want)); st != "valid" || p == nil || *p != want {
		t.Fatalf("state %q pin %v", st, p)
	}
	if e, _ := s.svc.GetRegistryEntry(ctx, "nds"); e.Firmware[0].Pinned != want {
		t.Fatalf("%+v", e.Firmware[0])
	}
	if _, st, _ := state(pin(hex.EncodeToString(make([]byte, 32)))); st != "mismatch" {
		t.Fatalf("state %q", st)
	}
	if _, st, p := state(pin("")); st == "mismatch" || p != nil {
		t.Fatalf("state %q pin %v", st, p)
	}
	wantStatus(t, s.do("POST", base+"/pin", map[string]any{"sha256": "xyz"}, opt{token: tok.token, raw: true}), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/systems/nds/firmware/nope/pin", map[string]any{"sha256": ""}, tok), 404, "")

	// Remove.
	rec = s.do("DELETE", base, nil, tok)
	wantStatus(t, rec, 200, "")
	if present, _, _ := state(decode[adminSysT](t, rec)); present {
		t.Fatal("still present in the answer")
	}
	if _, _, err := s.svc.OpenFirmware(ctx, "nds", "bios7"); err == nil {
		t.Fatal("file still stored")
	}
	wantStatus(t, s.do("GET", base, nil, tok), 404, "")

	// The member route keeps working for members and does not accept the admin routes.
	anna := s.device(s.anna.ID, "Annas PC")
	put(b7)
	wantStatus(t, s.do("GET", "/api/v1/systems/nds/firmware/bios7", nil, opt{token: anna.tok}), 200, "")
	wantStatus(t, s.do("GET", base, nil, opt{token: anna.tok}), 403, "forbidden")
}
