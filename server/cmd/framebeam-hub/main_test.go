package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestSetupAdminTwice(t *testing.T) {
	dir := t.TempDir()
	var out bytes.Buffer
	if err := runSetupAdmin([]string{"-data-dir", dir, "-username", "fabio"}, strings.NewReader("geheim-1234\n"), &out); err != nil {
		t.Fatal(err)
	}
	if strings.Contains(out.String(), "geheim") {
		t.Fatal("Passwort in der Ausgabe")
	}
	err := runSetupAdmin([]string{"-data-dir", dir, "-username", "zweiter"}, strings.NewReader("geheim-1234\n"), &out)
	if err == nil {
		t.Fatal("zweiter Admin muss verweigert werden")
	}
	if err := runSetupAdmin([]string{"-data-dir", dir}, strings.NewReader("x\n"), &out); err == nil {
		t.Fatal("ohne -username muss scheitern")
	}
}
