//go:build !windows

package main

// grantFolder is Windows only.
func grantFolder(string) error { return errGrantUnsupported }
