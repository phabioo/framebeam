//go:build linux

package updates

import (
	"os"
	"syscall"
)

// dirOwner returns the owner of a directory.
func dirOwner(fi os.FileInfo) (uid, gid int, ok bool) {
	st, isStat := fi.Sys().(*syscall.Stat_t)
	if !isStat {
		return 0, 0, false
	}
	return int(st.Uid), int(st.Gid), true
}
