//go:build !linux && !windows

package sysmetrics

func readCPU() (uint64, uint64, bool)      { return 0, 0, false }
func systemMemory() (uint64, uint64, bool) { return 0, 0, false }
func disk(string) (uint64, uint64, bool)   { return 0, 0, false }
