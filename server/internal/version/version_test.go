package version

import "testing"

func TestStringDefault(t *testing.T) {
	if got := String(); got != "dev" {
		t.Fatalf("String() = %q, want %q", got, "dev")
	}
}

func TestStringOverride(t *testing.T) {
	old := Version
	t.Cleanup(func() { Version = old })
	Version = "1.2.3"
	if got := String(); got != "1.2.3" {
		t.Fatalf("String() = %q, want %q", got, "1.2.3")
	}
}

func TestChannelAndCommitDefaults(t *testing.T) {
	if Channel != "dev" || Commit != "" {
		t.Fatalf("Channel=%q Commit=%q", Channel, Commit)
	}
}
