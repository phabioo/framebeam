//go:build !linux && !windows

package hub

func freeBytes(string) uint64 { return 0 }
