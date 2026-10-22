package updates

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// MSIMarkerName is a file next to the server executable that the MSI installer creates. Its presence marks an install
// by the MSI (the Windows counterpart of /usr/bin/uzume-server for the .deb).
const MSIMarkerName = "uzume-server.msi-installed"

// MSILogName is the msiexec log, copied to <data>/updates after the installation.
const MSILogName = "msiexec.log"

// ContainerEnv is set to "1" in the official Docker image (packaging/docker/Dockerfile). A server in a container never
// installs updates itself: it shows that one is available and the admin pulls a newer image.
const ContainerEnv = "UZUME_SERVER_CONTAINER"

// InContainer reports whether the server runs in the official container image.
func InContainer() bool { return os.Getenv(ContainerEnv) == "1" }

// Platform is the platform string of this binary, e.g. linux-amd64 or windows-amd64 (server naming; the app's
// Windows platform in the index is windows-x64).
func Platform() string { return runtime.GOOS + "-" + runtime.GOARCH }

func isWindowsPlatform(platform string) bool { return strings.HasPrefix(platform, "windows-") }

// KindFor is the artifact kind the server queries and applies on a platform: msi on Windows, deb elsewhere.
func KindFor(platform string) string {
	if isWindowsPlatform(platform) {
		return KindMSI
	}
	return KindDeb
}

// IsPackaged reports whether executable belongs to a package install that the update helper can replace: on
// Linux the .deb path, on Windows the MSI marker file next to the executable; in both cases requestDir must be
// writable. A server in the container image (InContainer) is never packaged.
func IsPackaged(platform, executable, requestDir string) bool {
	if InContainer() {
		return false
	}
	if isWindowsPlatform(platform) {
		if executable == "" {
			return false
		}
		fi, err := os.Lstat(filepath.Join(filepath.Dir(executable), MSIMarkerName))
		if err != nil || !fi.Mode().IsRegular() {
			return false
		}
		return RequestDirWritable(requestDir)
	}
	return executable == PackagedExecutable && RequestDirWritable(requestDir)
}

// InstallCommand is the privileged install command for the (private, verified) package copy.
func InstallCommand(platform, pkgPath, logPath string) (name string, args []string) {
	if isWindowsPlatform(platform) {
		return "msiexec.exe", []string{"/i", pkgPath, "/qn", "/norestart", "/l*v", logPath, "ALLUSERS=1"}
	}
	return "dpkg", []string{"-i", pkgPath}
}

// DefaultPrivateDir is the parent of the updater's private temp directory: on Windows
// %ProgramData%\Uzume\ServerUpdater (the installer restricts its ACL to SYSTEM and Administrators), else "" (system
// temp, which is private to root on Linux through the 0700 MkdirTemp).
func DefaultPrivateDir(goos string, getenv func(string) string) string {
	if goos != "windows" {
		return ""
	}
	pd := getenv("ProgramData")
	if pd == "" {
		pd = `C:\ProgramData`
	}
	return filepath.Join(pd, "Uzume", "ServerUpdater")
}
