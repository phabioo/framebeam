//go:build !linux

package hub

func freeBytes(string) uint64 { return 0 }
