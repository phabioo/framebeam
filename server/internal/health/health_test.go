package health

import "testing"

func TestMostUrgentAndSort(t *testing.T) {
	if MostUrgent(nil) != nil {
		t.Fatal("no problems, no banner")
	}
	ps := []Problem{{ID: "a", Severity: Warn}, {ID: "b", Severity: Error}, {ID: "c", Severity: Warn}, {ID: "d", Severity: Error}}
	if got := MostUrgent(ps); got.ID != "b" {
		t.Fatalf("most urgent = %s, want the first error", got.ID)
	}
	Sort(ps)
	var order string
	for _, p := range ps {
		order += p.ID
	}
	if order != "bdac" {
		t.Fatalf("order = %s, want bdac (stable within a severity)", order)
	}
	one := []Problem{{ID: "x", Severity: Warn}, {ID: "y", Severity: Warn}}
	if MostUrgent(one).ID != "x" {
		t.Fatal("equal severity: the first check wins")
	}
}
