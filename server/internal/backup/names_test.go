package backup

import "testing"

func TestFindRejectsUnsafeNames(t *testing.T) {
	dir := t.TempDir()
	for _, n := range []string{"../x.tar", "a/b.tar", `a\b.tar`, "..", "", "/etc/passwd"} {
		if _, err := Find(dir, n); err == nil {
			t.Errorf("Find(%q) accepted", n)
		}
		if SafeName(n) {
			t.Errorf("SafeName(%q) true", n)
		}
	}
}
