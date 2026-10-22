package hub

import (
	"strings"
	"testing"
)

func TestManualCommandQuotesTheURL(t *testing.T) {
	a := &UpdateAvailable{URL: "https://example.org/a;b'c$(id).deb", Artifact: "framebeam-hub_1.0.0_amd64.deb"}
	got := manualCommand("linux-amd64", a)
	want := `curl -fLO 'https://example.org/a;b'\''c$(id).deb' && sudo apt install ./framebeam-hub_1.0.0_amd64.deb`
	if got != want {
		t.Fatalf("got  %s\nwant %s", got, want)
	}
}

func TestManualCommandInContainer(t *testing.T) {
	t.Setenv("FRAMEBEAM_HUB_CONTAINER", "1")
	got := manualCommand("linux-amd64", &UpdateAvailable{URL: "https://example.org/a.deb", Artifact: "a.deb"})
	if !strings.Contains(got, "pulling the new image") || strings.Contains(got, "apt") {
		t.Fatal(got)
	}
}
