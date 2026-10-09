//go:build windows

package hub

import "golang.org/x/sys/windows"

func freeBytes(dir string) uint64 {
	p, err := windows.UTF16PtrFromString(dir)
	if err != nil {
		return 0
	}
	var avail, total, free uint64
	if err := windows.GetDiskFreeSpaceEx(p, &avail, &total, &free); err != nil {
		return 0
	}
	return avail
}
