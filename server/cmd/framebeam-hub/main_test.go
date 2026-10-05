package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestSetupAdminTwice(t *testing.T) {
	dir := t.TempDir()
	var out bytes.Buffer
	if err := runSetupAdmin([]string{"-data-dir", dir, "-username", "fabio"}, strings.NewReader("secret-1234\n"), &out); err != nil {
		t.Fatal(err)
	}
	if strings.Contains(out.String(), "secret") {
		t.Fatal("password in output")
	}
	err := runSetupAdmin([]string{"-data-dir", dir, "-username", "second"}, strings.NewReader("secret-1234\n"), &out)
	if err == nil {
		t.Fatal("second admin must be refused")
	}
	if err := runSetupAdmin([]string{"-data-dir", dir}, strings.NewReader("x\n"), &out); err == nil {
		t.Fatal("missing -username must fail")
	}
}
