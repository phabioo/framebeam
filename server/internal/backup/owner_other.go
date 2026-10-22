//go:build !unix

package backup

// MatchOwner is not needed on this platform.
func MatchOwner(tree, ref string) error { return nil }
