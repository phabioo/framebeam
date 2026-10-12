package pluginpkg

import (
	"reflect"
	"strings"
	"testing"
)

func rsManifest(rs string) []byte {
	return []byte(`{"api":1,"id":"net.example.tracker","name":"T","version":"1.0.0","entry":"main.lua","run_settings":` + rs + `}`)
}

const soulLinkSettings = `[
 {"key":"mode","type":"enum","label":"run.mode","hint":"run.mode_hint","default":"soullink","future":1,
  "options":[{"value":"nuzlocke","label":"run.mode.nuzlocke"},{"value":"soullink","label":"run.mode.soullink"}]},
 {"key":"rules.first_encounter_only","type":"boolean","label":"rules.first","default":true},
 {"key":"rules.dupes_clause","type":"boolean","label":"rules.dupes","default":false}]`

func TestRunSettingsValid(t *testing.T) {
	m, err := ParseManifest(rsManifest(soulLinkSettings))
	if err != nil {
		t.Fatal(err)
	}
	if len(m.RunSettings) != 3 || m.RunSettings[0].Default != "soullink" || m.RunSettings[1].Default != true ||
		len(m.RunSettings[0].Options) != 2 || m.RunSettings[2].Hint != "" {
		t.Fatalf("parsed %+v", m.RunSettings)
	}
	if m, err = ParseManifest([]byte(strings.Replace(string(rsManifest("null")), `"run_settings":null`, `"x":1`, 1))); err != nil || m.RunSettings != nil {
		t.Fatalf("no run_settings: %v %v", m.RunSettings, err)
	}
}

func TestRunSettingsInvalid(t *testing.T) {
	bool1 := func(key string) string {
		return `{"key":"` + key + `","type":"boolean","label":"l","default":true}`
	}
	opts := `"options":[{"value":"a","label":"la"},{"value":"b","label":"lb"}]`
	var many []string
	for i := 0; i < 33; i++ {
		many = append(many, bool1("k"+strings.Repeat("a", i%5)+string(rune('a'+i%26))+string(rune('a'+i/26))))
	}
	tests := []struct{ name, rs, want string }{
		{"not array", `{}`, "must be an array"},
		{"empty", `[]`, "1 to 32 entries"},
		{"too many", `[` + strings.Join(many, ",") + `]`, "1 to 32 entries"},
		{"entry not object", `[1]`, "run_settings[0] must be an object"},
		{"key missing", `[{"type":"boolean","label":"l","default":true}]`, "run_settings[0].key"},
		{"key upper", `[` + bool1("Mode") + `]`, "run_settings[0].key"},
		{"key three segments", `[` + bool1("a.b.c") + `]`, "more than 2 segments"},
		{"key empty segment", `[` + bool1("a.") + `]`, "run_settings[0].key"},
		{"key long segment", `[` + bool1(strings.Repeat("a", 33)) + `]`, "run_settings[0].key"},
		{"key digit first", `[` + bool1("1a") + `]`, "run_settings[0].key"},
		{"duplicate key", `[` + bool1("a") + `,` + bool1("a") + `]`, "run_settings[1].key"},
		{"prefix key", `[` + bool1("rules") + `,` + bool1("rules.x") + `]`, "prefix"},
		{"prefix key reversed", `[` + bool1("rules.x") + `,` + bool1("rules") + `]`, "prefix"},
		{"bad type", `[{"key":"a","type":"int","label":"l","default":1}]`, "run_settings[0].type"},
		{"label missing", `[{"key":"a","type":"boolean","default":true}]`, "run_settings[0].label is required"},
		{"label bad", `[{"key":"a","type":"boolean","label":"has space","default":true}]`, "run_settings[0].label"},
		{"hint bad", `[{"key":"a","type":"boolean","label":"l","hint":"x/y","default":true}]`, "run_settings[0].hint"},
		{"default missing", `[{"key":"a","type":"boolean","label":"l"}]`, "run_settings[0].default is required"},
		{"boolean default string", `[{"key":"a","type":"boolean","label":"l","default":"yes"}]`, "default must be a boolean"},
		{"boolean with options", `[{"key":"a","type":"boolean","label":"l","default":true,` + opts + `}]`, "options is only allowed for type enum"},
		{"enum without options", `[{"key":"a","type":"enum","label":"l","default":"a"}]`, "options is required"},
		{"enum one option", `[{"key":"a","type":"enum","label":"l","default":"a","options":[{"value":"a","label":"la"}]}]`, "2 to 8 entries"},
		{"enum nine options", `[{"key":"a","type":"enum","label":"l","default":"a","options":[` +
			strings.TrimSuffix(strings.Repeat(`{"value":"a","label":"la"},`, 9), ",") + `]}]`, "2 to 8 entries"},
		{"option value bad", `[{"key":"a","type":"enum","label":"l","default":"a","options":[{"value":"A","label":"la"},{"value":"b","label":"lb"}]}]`, "options[0].value"},
		{"option value dup", `[{"key":"a","type":"enum","label":"l","default":"a","options":[{"value":"a","label":"la"},{"value":"a","label":"lb"}]}]`, "options[1].value"},
		{"option label missing", `[{"key":"a","type":"enum","label":"l","default":"a","options":[{"value":"a"},{"value":"b","label":"lb"}]}]`, "options[0].label is required"},
		{"option label bad", `[{"key":"a","type":"enum","label":"l","default":"a","options":[{"value":"a","label":"l a"},{"value":"b","label":"lb"}]}]`, "options[0].label"},
		{"enum default not option", `[` + bool1("x") + `,` + bool1("y") + `,{"key":"a","type":"enum","label":"l","default":"c",` + opts + `}]`,
			"run_settings[2].default must be one of the option values"},
		{"enum default bool", `[{"key":"a","type":"enum","label":"l","default":true,` + opts + `}]`, "default must be one of the option values"},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			_, err := ParseManifest(rsManifest(tc.rs))
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("error %v, want containing %q", err, tc.want)
			}
		})
	}
}

func TestRunSettingsStrings(t *testing.T) {
	man := strings.Replace(goodManifest, `"permissions"`, `"run_settings":`+soulLinkSettings+`,"permissions"`, 1)
	data := mkzip(t,
		zf{name: "manifest.json", body: man}, zf{name: "main.lua", body: "return {}"},
		zf{name: "i18n/en.json", body: `{"run":{"mode":"Mode","mode_hint":"Hint","mode.nuzlocke":"Nuzlocke","mode.soullink":"SoulLink"},"rules.first":"First","unused":"x","rules.dupes":5}`},
		zf{name: "i18n/pt-BR.json", body: `{"rules.first":"Primeiro"}`},
		zf{name: "i18n/broken.json", body: `[`}, zf{name: "i18n/notes.txt", body: "x"}, zf{name: "i18n/sub/de.json", body: `{}`})
	p, err := Open(data)
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]map[string]string{
		"en":    {"run.mode": "Mode", "run.mode_hint": "Hint", "run.mode.nuzlocke": "Nuzlocke", "run.mode.soullink": "SoulLink", "rules.first": "First"},
		"pt-BR": {"rules.first": "Primeiro"},
	}
	if got := p.RunSettingsStrings(); !reflect.DeepEqual(got, want) {
		t.Fatalf("strings %v, want %v", got, want)
	}
	p2, err := Open(good(t, zf{name: "i18n/en.json", body: `{"a":"b"}`}))
	if err != nil || p2.RunSettingsStrings() != nil {
		t.Fatalf("no run_settings: %v %v", err, p2.RunSettingsStrings())
	}
}
