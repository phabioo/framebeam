package hub

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
	"testing/fstest"

	"github.com/phabioo/uzume/server/internal/store"
)

func TestSystemDefsLoad(t *testing.T) {
	defs, err := loadSystemDefs()
	if err != nil {
		t.Fatal(err)
	}
	var ids []string
	for _, d := range defs {
		ids = append(ids, d.ID)
	}
	if got := strings.Join(ids, ","); got != "3ds,gb,gba,gbc,nds" {
		t.Fatal(got)
	}
}

func TestValidateSystemDefs(t *testing.T) {
	ok := func() systemDef {
		return systemDef{ID: "x", DisplayName: "X", Extensions: []string{".x"}, LibretroIDs: []string{"x"}, Platforms: []string{"linux-x86_64"},
			InputProfile: "x", DisplayProfile: "single_screen"}
	}
	if err := validateSystemDefs([]systemDef{ok()}); err != nil {
		t.Fatal(err)
	}
	bad := map[string]func(d *systemDef){
		"id":        func(d *systemDef) { d.ID = "Bad Id" },
		"ext case":  func(d *systemDef) { d.Extensions = []string{".GB"} },
		"ext dot":   func(d *systemDef) { d.Extensions = []string{"gb"} },
		"no ext":    func(d *systemDef) { d.Extensions = nil },
		"no libret": func(d *systemDef) { d.LibretroIDs = nil },
		"core id":   func(d *systemDef) { d.ProfiledCores = []string{"Bad/Core"} },
		"identity":  func(d *systemDef) { d.Identity = "bogus" },
		"fw":        func(d *systemDef) { d.Firmware = []firmwareDef{{ID: "a", DisplayName: "A"}} },
	}
	for name, mod := range bad {
		d := ok()
		mod(&d)
		if err := validateSystemDefs([]systemDef{d}); err == nil {
			t.Errorf("%s: want an error", name)
		}
	}
	a, b := ok(), ok()
	b.ID = "y"
	if err := validateSystemDefs([]systemDef{a, b}); err == nil || !strings.Contains(err.Error(), "used by both") {
		t.Errorf("duplicate extension: %v", err)
	}
	// File name and id must agree; unknown fields are rejected.
	fsys := fstest.MapFS{"s/a.json": {Data: []byte(`{"id":"b"}`)}}
	if _, err := loadSystemDefsFS(fsys, "s"); err == nil {
		t.Error("id/file name mismatch accepted")
	}
	fsys = fstest.MapFS{"s/a.json": {Data: []byte(`{"id":"a","bogus":1}`)}}
	if _, err := loadSystemDefsFS(fsys, "s"); err == nil {
		t.Error("unknown field accepted")
	}
}

func openTestDB(t *testing.T) (*Service, func(q string, a ...any)) {
	t.Helper()
	db, err := store.Open(filepath.Join(t.TempDir(), "uzume.db"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })
	return &Service{db: db}, func(q string, a ...any) {
		t.Helper()
		if _, err := db.Exec(q, a...); err != nil {
			t.Fatal(err)
		}
	}
}

func TestReconcileSystemsFreshAndTwice(t *testing.T) {
	s, _ := openTestDB(t)
	// After migrations only nds and 3ds exist; reconcile adds the rest and is idempotent.
	for i := 0; i < 2; i++ {
		if err := reconcileSystems(context.Background(), s.db); err != nil {
			t.Fatal(err)
		}
	}
	reg, err := s.ListRegistry(context.Background())
	if err != nil || len(reg) != 5 {
		t.Fatalf("%v %+v", err, reg)
	}
	gba := reg[2]
	if gba.ID != "gba" || gba.Name != "Game Boy Advance" || gba.FirmwareMode != FirmwareBuiltin || gba.CoreID != "" ||
		strings.Join(gba.Extensions, ",") != ".gba" || strings.Join(gba.LibretroIDs, ",") != "game_boy_advance" ||
		gba.InputProfile != "gba" || gba.DisplayProfile != "single_screen" || len(gba.Firmware) != 1 || gba.Firmware[0].ID != "gba_bios" ||
		len(gba.Firmware[0].Sizes) != 1 || gba.Firmware[0].Sizes[0] != 16384 {
		t.Fatalf("%+v", gba)
	}
	var n int
	s.db.QueryRow(`SELECT COUNT(*) FROM profiled_cores`).Scan(&n)
	if n != 5 { // melondsds, desmume, azahar, mgba, gambatte
		t.Fatalf("profiled cores: %d", n)
	}
	var pc, pn string
	if err := s.db.QueryRow(`SELECT preferred_core_id, preferred_core_name FROM systems WHERE id = 'gb'`).Scan(&pc, &pn); err != nil || pc != "" || pn != "" {
		t.Fatalf("%q %q %v", pc, pn, err)
	}
}

func TestReconcileKeepsAdminState(t *testing.T) {
	s, exec := openTestDB(t)
	ctx := context.Background()
	if err := reconcileSystems(ctx, s.db); err != nil {
		t.Fatal(err)
	}
	// Admin state plus stale definition columns.
	exec(`UPDATE systems SET firmware_mode = 'native', default_core_id = 'desmume', preferred_core_id = 'melonds_ds',
		preferred_core_name = 'melonDS DS', expected_core_version = '1.4.0' WHERE id = 'nds'`)
	exec(`UPDATE systems SET display_name = 'Old', extensions = '.old', libretro_ids = 'old', provisioning = 'old', input_profile = 'old',
		display_profile = 'old', platforms = 'old' WHERE id = 'gb'`)
	exec(`INSERT INTO system_cores(system_id, core_id, version, installed_at, origin, display_name, license) VALUES ('nds','desmume','1',1,'x','D','L')`)
	exec(`INSERT INTO firmware_files(system_id, file_id, sha256, size, uploaded_at) VALUES ('nds','bios7','aa',16384,1)`)
	exec(`INSERT INTO firmware_pins(system_id, file_id, sha256) VALUES ('nds','bios9','bb')`)
	exec(`UPDATE firmware_defs SET display_name = 'old', sizes = '1', sort = 9 WHERE system_id = 'nds' AND file_id = 'bios7'`)
	exec(`INSERT INTO firmware_defs(system_id, file_id, display_name, sizes, sort) VALUES ('nds','legacy','Legacy','1',7)`)
	exec(`INSERT INTO firmware_files(system_id, file_id, sha256, size, uploaded_at) VALUES ('nds','legacy','cc',1,1)`)
	exec(`INSERT INTO systems(id, display_name, extensions, preferred_core_id, preferred_core_name, platforms, provisioning, input_profile,
		display_profile, firmware_mode, libretro_ids) VALUES ('custom','Custom','.cus','','','linux-x86_64','p','c','single_screen','builtin','custom')`)
	exec(`INSERT INTO profiled_cores(core_id) VALUES ('stale')`)

	if err := reconcileSystems(ctx, s.db); err != nil {
		t.Fatal(err)
	}
	var mode, def, pc, ev string
	s.db.QueryRow(`SELECT firmware_mode, default_core_id, preferred_core_id, expected_core_version FROM systems WHERE id = 'nds'`).Scan(&mode, &def, &pc, &ev)
	if mode != "native" || def != "desmume" || pc != "melonds_ds" || ev != "1.4.0" {
		t.Fatalf("admin state changed: %s %s %s %s", mode, def, pc, ev)
	}
	e, err := s.GetRegistryEntry(ctx, "gb")
	if err != nil || e.Name != "Game Boy" || strings.Join(e.Extensions, ",") != ".gb" || e.InputProfile != "gb" || e.DisplayProfile != "single_screen" ||
		e.Provisioning != "Installed by the admin" || strings.Join(e.Platforms, ",") != "windows-x86_64,linux-x86_64" ||
		strings.Join(e.LibretroIDs, ",") != "game_boy,game_boy_color,game_boy_advance" {
		t.Fatalf("%v %+v", err, e)
	}
	var n int
	s.db.QueryRow(`SELECT COUNT(*) FROM system_cores WHERE system_id = 'nds'`).Scan(&n)
	var files, pins, custom, legacy, stale int
	s.db.QueryRow(`SELECT COUNT(*) FROM firmware_files`).Scan(&files)
	s.db.QueryRow(`SELECT COUNT(*) FROM firmware_pins`).Scan(&pins)
	s.db.QueryRow(`SELECT COUNT(*) FROM systems WHERE id = 'custom'`).Scan(&custom)
	s.db.QueryRow(`SELECT COUNT(*) FROM firmware_defs WHERE file_id = 'legacy'`).Scan(&legacy)
	s.db.QueryRow(`SELECT COUNT(*) FROM profiled_cores WHERE core_id = 'stale'`).Scan(&stale)
	if n != 1 || files != 2 || pins != 1 || custom != 1 || legacy != 1 || stale != 0 {
		t.Fatalf("cores=%d files=%d pins=%d custom=%d legacy=%d stale=%d", n, files, pins, custom, legacy, stale)
	}
	var name, sizes string
	var sort int
	s.db.QueryRow(`SELECT display_name, sizes, sort FROM firmware_defs WHERE system_id = 'nds' AND file_id = 'bios7'`).Scan(&name, &sizes, &sort)
	if name != "ARM7 BIOS" || sizes != "16384" || sort != 1 {
		t.Fatalf("%s %s %d", name, sizes, sort)
	}
}

// The nds and 3ds files reproduce what the migrations seeded.
func TestSystemDefsMatchMigrationSeed(t *testing.T) {
	s, _ := openTestDB(t)
	ctx := context.Background()
	before, err := s.ListRegistry(ctx)
	if err != nil || len(before) != 2 {
		t.Fatalf("%v %d", err, len(before))
	}
	if err := reconcileSystems(ctx, s.db); err != nil {
		t.Fatal(err)
	}
	after, err := s.ListRegistry(ctx)
	if err != nil {
		t.Fatal(err)
	}
	for _, b := range before {
		for _, a := range after {
			if a.ID != b.ID {
				continue
			}
			ja, _ := json.Marshal(a)
			jb, _ := json.Marshal(b)
			if string(ja) != string(jb) {
				t.Errorf("%s differs from the migration seed:\n%s\n%s", a.ID, jb, ja)
			}
		}
	}
}

func TestMatchesCoreExtensionFilter(t *testing.T) {
	sys := func(id string, ext string, ids ...string) SystemEntry {
		return SystemEntry{ID: id, Extensions: strings.Split(ext, ","), LibretroIDs: ids}
	}
	gb := sys("gb", ".gb", "game_boy", "game_boy_color", "game_boy_advance")
	gba := sys("gba", ".gba", "game_boy_advance")
	core := func(sid string, ext ...string) coreInfo { return coreInfo{SystemID: sid, Extensions: ext} }
	cases := []struct {
		name string
		e    SystemEntry
		c    coreInfo
		want bool
	}{
		{"mgba gb", gb, core("game_boy_advance", "gba", "gb", "gbc", "sgb"), true},
		{"mgba gba", gba, core("game_boy_advance", "gba", "gb", "gbc"), true},
		{"gambatte gb", gb, core("game_boy", "gb", "gbc", "dmg"), true},
		{"gambatte gba", gba, core("game_boy", "gb", "gbc"), false},
		{"gpsp gb", gb, core("game_boy_advance", "gba", "bin"), false},
		{"gpsp gba", gba, core("game_boy_advance", "gba", "bin"), true},
		{"case and dot", gba, core("GAME_BOY_ADVANCE", ".GBA"), true},
		{"no extensions", gba, core("game_boy_advance"), true},
		{"wrong system id", gba, core("nds", "gba"), false},
		{"empty system id", gba, core("", "gba"), false},
	}
	for _, tc := range cases {
		if got := tc.e.matchesCore(tc.c); got != tc.want {
			t.Errorf("%s: got %v", tc.name, got)
		}
	}
}

func TestSystemForFilenameGameBoy(t *testing.T) {
	systems := []System{{ID: "gb", Extensions: []string{".gb"}}, {ID: "gba", Extensions: []string{".gba"}}, {ID: "gbc", Extensions: []string{".gbc"}},
		{ID: "nds", Extensions: []string{".nds"}}}
	for file, want := range map[string]string{"a.gb": "gb", "A.GBC": "gbc", "x.y.gba": "gba", "a.nds": "nds"} {
		if got, ok := systemForFilename(systems, file); !ok || got != want {
			t.Errorf("%s: %q %v", file, got, ok)
		}
	}
	if _, ok := systemForFilename(systems, "a.sgb"); ok {
		t.Error("unknown extension matched")
	}
}

// Every server system has an app system manifest and every profiled core an app core manifest.
func TestHubSystemsMatchPlayerManifests(t *testing.T) {
	root := filepath.Join("..", "..", "..", "client", "emulation", "manifests")
	if _, err := os.Stat(root); err != nil {
		t.Skip("client manifests not found")
	}
	defs, err := loadSystemDefs()
	if err != nil {
		t.Fatal(err)
	}
	read := func(path string, v any) bool {
		data, err := os.ReadFile(path)
		if err != nil {
			t.Errorf("missing app manifest %s", path)
			return false
		}
		if err := json.Unmarshal(data, v); err != nil {
			t.Errorf("%s: %v", path, err)
			return false
		}
		return true
	}
	for _, d := range defs {
		var m struct {
			SystemID    string   `json:"system_id"`
			Identity    string   `json:"identity"`
			DisplayName string   `json:"display_name"`
			Extensions  []string `json:"extensions"`
			Firmware    struct {
				Files []struct{ ID, Name string } `json:"files"`
			} `json:"firmware"`
		}
		if read(filepath.Join(root, "systems", d.ID+".json"), &m) {
			if m.Identity != d.Identity {
				t.Errorf("%s: identity differs: %q vs %q", d.ID, m.Identity, d.Identity)
			}
			if m.SystemID != d.ID || m.DisplayName != d.DisplayName {
				t.Errorf("%s: system_id/display_name differ: %q %q", d.ID, m.SystemID, m.DisplayName)
			}
			a, b := append([]string(nil), m.Extensions...), append([]string(nil), d.Extensions...)
			sort.Strings(a)
			sort.Strings(b)
			if strings.Join(a, ",") != strings.Join(b, ",") {
				t.Errorf("%s: extensions differ: %v vs %v", d.ID, a, b)
			}
			var pf, hf []string
			for _, f := range m.Firmware.Files {
				pf = append(pf, f.ID+"="+f.Name)
			}
			for _, f := range d.Firmware {
				hf = append(hf, f.ID+"="+f.File)
			}
			sort.Strings(pf)
			sort.Strings(hf)
			if strings.Join(pf, ",") != strings.Join(hf, ",") {
				t.Errorf("%s: firmware files differ: %v vs %v", d.ID, pf, hf)
			}
		}
		for _, c := range d.ProfiledCores {
			var cm struct {
				SystemIDs []string `json:"system_ids"`
			}
			if read(filepath.Join(root, "cores", c+".json"), &cm) {
				found := false
				for _, id := range cm.SystemIDs {
					found = found || id == d.ID
				}
				if !found {
					t.Errorf("core %s: system_ids %v lack %s", c, cm.SystemIDs, d.ID)
				}
			}
		}
	}
}
