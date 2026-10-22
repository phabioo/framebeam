//go:build unix

package backup

import (
	"io/fs"
	"os"
	"path/filepath"
	"syscall"
)

// MatchOwner gives tree the owner of ref. `framebeam-hub restore` run as root unpacks files the Hub user (who
// owns the data directory) must be able to write; without this the restored Hub could not save anything.
// It does nothing when not run as root or when the owner already matches.
func MatchOwner(tree, ref string) error {
	if os.Geteuid() != 0 {
		return nil
	}
	st, err := os.Stat(ref)
	if err != nil {
		return err
	}
	sys, ok := st.Sys().(*syscall.Stat_t)
	if !ok || (sys.Uid == 0 && sys.Gid == 0) {
		return nil
	}
	return filepath.WalkDir(tree, func(p string, _ fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		return os.Lchown(p, int(sys.Uid), int(sys.Gid))
	})
}
