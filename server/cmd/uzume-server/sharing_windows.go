//go:build windows

package main

import (
	"errors"
	"fmt"
	"strconv"
	"time"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/registry"
	"golang.org/x/sys/windows/svc"
	"golang.org/x/sys/windows/svc/mgr"
)

// winSharing is the sharingSystem of a Windows machine; the commands run elevated.
type winSharing struct{}

func newSharingSystem() sharingSystem { return winSharing{} }

// withService opens a service by name with full access and runs fn on it; exists=false if it is not registered.
func withService(name string, fn func(*mgr.Service) error) (exists bool, err error) {
	m, err := mgr.Connect()
	if err != nil {
		return false, fmt.Errorf("connect to the service manager (administrator rights needed): %w", err)
	}
	defer m.Disconnect()
	s, err := m.OpenService(name)
	if err != nil {
		if errors.Is(err, windows.ERROR_SERVICE_DOES_NOT_EXIST) {
			return false, nil
		}
		return false, err
	}
	defer s.Close()
	if fn == nil {
		return true, nil
	}
	return true, fn(s)
}

func (winSharing) ServiceExists(name string) (bool, error) { return withService(name, nil) }

func (winSharing) StopService(name string, wait time.Duration) error {
	ok, err := withService(name, func(s *mgr.Service) error {
		deadline := time.Now().Add(wait)
		sent := false
		for {
			q, err := s.Query()
			if err != nil {
				return err
			}
			switch q.State {
			case svc.Stopped:
				return nil
			case svc.StopPending:
			default:
				if !sent {
					if _, err := s.Control(svc.Stop); err != nil && !errors.Is(err, windows.ERROR_SERVICE_NOT_ACTIVE) {
						return err
					}
					sent = true
				}
			}
			if time.Now().After(deadline) {
				return fmt.Errorf("did not stop within %d s", int(wait.Seconds()))
			}
			time.Sleep(250 * time.Millisecond)
		}
	})
	if err == nil && !ok {
		err = errors.New("not installed")
	}
	return err
}

func (winSharing) StartService(name string, wait time.Duration) error {
	ok, err := withService(name, func(s *mgr.Service) error {
		deadline := time.Now().Add(wait)
		started, stoppedAfterStart := false, 0
		for {
			q, err := s.Query()
			if err != nil {
				return err
			}
			switch q.State {
			case svc.Running:
				return nil
			case svc.Stopped:
				if started {
					if stoppedAfterStart++; stoppedAfterStart >= 4 {
						return errors.New("stopped again right after starting (see the server log)")
					}
				} else {
					if err := s.Start(); err != nil && !errors.Is(err, windows.ERROR_SERVICE_ALREADY_RUNNING) {
						return err
					}
					started = true
				}
			}
			if time.Now().After(deadline) {
				return fmt.Errorf("not running within %d s", int(wait.Seconds()))
			}
			time.Sleep(250 * time.Millisecond)
		}
	})
	if err == nil && !ok {
		err = errors.New("not installed")
	}
	return err
}

func (winSharing) SetStartType(name string, start serviceStart) error {
	ok, err := withService(name, func(s *mgr.Service) error {
		cfg, err := s.Config()
		if err != nil {
			return err
		}
		if start == startDisabled {
			cfg.StartType = mgr.StartDisabled
		} else {
			cfg.StartType = mgr.StartAutomatic
		}
		return s.UpdateConfig(cfg)
	})
	if err == nil && !ok {
		err = errors.New("not installed")
	}
	return err
}

func (winSharing) GrantModify(dir, account string) error {
	sid, _, _, err := windows.LookupSID("", account)
	if err != nil {
		return fmt.Errorf("account %q not found: %w", account, err)
	}
	// Modify = read, write, execute and delete (0x1301BF), inherited by sub-folders and files.
	const modify = windows.FILE_GENERIC_READ | windows.FILE_GENERIC_WRITE | windows.FILE_GENERIC_EXECUTE | windows.DELETE
	return addAllowEntry(dir, sid, modify)
}

// ServerPort reads the port the MSI recorded in HKLM\Software\Uzume\Server (DWORD, or a string).
func (winSharing) ServerPort() int {
	k, err := registry.OpenKey(registry.LOCAL_MACHINE, `Software\Uzume\Server`, registry.QUERY_VALUE)
	if err != nil {
		return 0
	}
	defer k.Close()
	if n, _, err := k.GetIntegerValue("Port"); err == nil {
		return int(n)
	}
	if s, _, err := k.GetStringValue("Port"); err == nil {
		if n, err := strconv.Atoi(s); err == nil {
			return n
		}
	}
	return 0
}
