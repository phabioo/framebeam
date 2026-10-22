package updates

import "testing"

func TestContainerNeverPackaged(t *testing.T) {
	req := t.TempDir()
	t.Setenv(ContainerEnv, "0")
	if !IsPackaged("linux-amd64", PackagedExecutable, req) {
		t.Fatal("precondition: the .deb path is packaged")
	}
	t.Setenv(ContainerEnv, "1")
	if !InContainer() || IsPackaged("linux-amd64", PackagedExecutable, req) {
		t.Fatal("a container Hub must not apply updates itself")
	}
}
