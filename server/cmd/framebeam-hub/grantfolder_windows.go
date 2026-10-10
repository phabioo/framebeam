//go:build windows

package main

import (
	"crypto/sha1"
	"encoding/binary"
	"fmt"
	"os"
	"strings"
	"unicode/utf16"

	"golang.org/x/sys/windows"
)

// grantFolder adds an inherited allow entry (GENERIC_READ | GENERIC_EXECUTE) for the Hub service account to the
// DACL of the folder. Existing entries stay; the call is idempotent in effect.
func grantFolder(path string) error {
	st, err := os.Stat(path)
	if err != nil {
		return err
	}
	if !st.IsDir() {
		return fmt.Errorf("%s is not a folder", path)
	}
	sid, err := hubServiceSID()
	if err != nil {
		return err
	}
	sd, err := windows.GetNamedSecurityInfo(path, windows.SE_FILE_OBJECT, windows.DACL_SECURITY_INFORMATION)
	if err != nil {
		return fmt.Errorf("read permissions of %s: %w", path, err)
	}
	dacl, _, err := sd.DACL()
	if err != nil {
		return fmt.Errorf("read permissions of %s: %w", path, err)
	}
	entry := windows.EXPLICIT_ACCESS{
		AccessPermissions: windows.GENERIC_READ | windows.GENERIC_EXECUTE,
		AccessMode:        windows.GRANT_ACCESS,
		Inheritance:       windows.SUB_CONTAINERS_AND_OBJECTS_INHERIT,
		Trustee: windows.TRUSTEE{
			TrusteeForm:  windows.TRUSTEE_IS_SID,
			TrusteeType:  windows.TRUSTEE_IS_UNKNOWN,
			TrusteeValue: windows.TrusteeValueFromSID(sid),
		},
	}
	merged, err := windows.ACLFromEntries([]windows.EXPLICIT_ACCESS{entry}, dacl)
	if err != nil {
		return fmt.Errorf("build permissions: %w", err)
	}
	if err := windows.SetNamedSecurityInfo(path, windows.SE_FILE_OBJECT, windows.DACL_SECURITY_INFORMATION, nil, nil, merged, nil); err != nil {
		return fmt.Errorf("set permissions of %s (administrator rights needed): %w", path, err)
	}
	return nil
}

// hubServiceSID resolves NT SERVICE\FrameBeamHub; if the account cannot be looked up (the service is not
// registered yet) the virtual service SID is derived from the name the way Windows does.
func hubServiceSID() (*windows.SID, error) {
	if sid, _, _, err := windows.LookupSID("", hubServiceAccount); err == nil {
		return sid, nil
	}
	name := strings.ToUpper(strings.TrimPrefix(hubServiceAccount, `NT SERVICE\`))
	sum := sha1.Sum(littleEndianUTF16(name))
	s := "S-1-5-80"
	for i := 0; i < 5; i++ {
		s += fmt.Sprintf("-%d", binary.LittleEndian.Uint32(sum[i*4:]))
	}
	return windows.StringToSid(s)
}

func littleEndianUTF16(s string) []byte {
	u := utf16.Encode([]rune(s))
	b := make([]byte, 2*len(u))
	for i, c := range u {
		binary.LittleEndian.PutUint16(b[2*i:], c)
	}
	return b
}
