package web

import (
	"strings"
	"testing"
)

func TestDownloadNames(t *testing.T) {
	const gid = "0a1b2c3d-1111-2222-3333-444455556666"
	for _, c := range []struct{ title, ascii, full string }{
		{"Harbor Rally", "Harbor_Rally-rev3.sav", "Harbor Rally-rev3.sav"},
		{"ポケットモンスター", "0a1b2c3d-rev3.sav", "ポケットモンスター-rev3.sav"},
		{"Pokémon: Diamond?", "Pok_mon_Diamond-rev3.sav", "Pokémon Diamond-rev3.sav"},
		{"", "0a1b2c3d-rev3.sav", "0a1b2c3d-rev3.sav"},
		{"save", "0a1b2c3d-rev3.sav", "save-rev3.sav"},
		{`a/b\c|d"e<f>`, "a_b_c_d_e_f-rev3.sav", "abcdef-rev3.sav"},
		{"Game. . ", "Game-rev3.sav", "Game-rev3.sav"},
		{"???", "0a1b2c3d-rev3.sav", "0a1b2c3d-rev3.sav"},
	} {
		ascii, full := downloadNames(c.title, gid, "rev3")
		if ascii != c.ascii || full != c.full {
			t.Errorf("%q: got %q / %q, want %q / %q", c.title, ascii, full, c.ascii, c.full)
		}
	}
}

func TestContentDisposition(t *testing.T) {
	got := contentDisposition("0a1b2c3d-rev3.sav", "ポケ x-rev3.sav")
	want := `attachment; filename="0a1b2c3d-rev3.sav"; filename*=UTF-8''%E3%83%9D%E3%82%B1%20x-rev3.sav`
	if got != want {
		t.Fatalf("%s", got)
	}
	if strings.ContainsAny(strings.SplitN(got, "''", 2)[1], " '()*\"") {
		t.Fatal("unescaped character")
	}
}
