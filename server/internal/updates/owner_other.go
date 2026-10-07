//go:build !linux

package updates

import "os"

func dirOwner(os.FileInfo) (uid, gid int, ok bool) { return 0, 0, false }
