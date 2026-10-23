//go:build !windows

package main

// newSharingSystem: the sharing commands are Windows only.
func newSharingSystem() sharingSystem { return nil }
