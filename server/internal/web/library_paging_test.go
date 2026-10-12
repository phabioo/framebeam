package web

import (
	"bytes"
	"fmt"
	"strings"
	"testing"
)

func TestLibrarySortAndLoadMore(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	users, _ := e.svc.ListUsers(bg)
	for i := 0; i < 30; i++ {
		title := fmt.Sprintf("Game %02d", i)
		// A unique 4-character code at 0x0C keeps the games apart (same code = same game); bigger with the number.
		rom := []byte(fmt.Sprintf("%012d%04d", 0, i) + strings.Repeat("x", i) + title)
		if _, err := e.svc.AddROM(bg, bytes.NewReader(rom), "g.nds", title, "", users[0].ID); err != nil {
			t.Fatal(err)
		}
	}
	hx := map[string]string{"HX-Request": "true", "HX-Target": "library-results"}
	rec := c.get("/library", nil)
	contains(t, rec, "Showing 25 of 30", "Load 5 more", `href="/library?n=50"`, "Name ▾")
	notContains(t, rec, "Game 29") // stays behind "Load more"
	rec = c.get("/library?n=50", hx)
	contains(t, rec, "Showing 30 of 30", `id="library-chips"`, `hx-swap-oob="true"`, "Game 29")
	notContains(t, rec, "Load ")
	// Sorted by size the largest comes first and the sort menu marks the active order.
	rec = c.get("/library?sort=size", nil)
	body := rec.Body.String()
	if i, j := strings.Index(body, "Game 29"), strings.Index(body, "Game 28"); i < 0 || j < 0 || i > j {
		t.Fatalf("size order: %d %d", i, j)
	}
	contains(t, rec, "Size ▾", `class="mi on" href="/library?sort=size"`)
	notContains(t, rec, "Game 04") // 30 games, 25 shown, smallest last
	// An unknown sort falls back to the name.
	contains(t, c.get("/library?sort=bogus", nil), "Name ▾")
}
