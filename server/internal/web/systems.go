package web

import (
	"errors"
	"net/http"
	"strings"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// maxFirmwareForm bounds the firmware upload form (the largest firmware image is 512 KiB).
const maxFirmwareForm = 1 << 20

type fwView struct {
	ID, Name, Requirement, Expected, Present, Size string
	State, StateLabel, Class                       string
	Pinned                                         bool
	PinFull                                        string
	HasFile                                        bool
	Sizes                                          string
}

type clientView struct {
	Device, Platform, Versions, Status, StatusText string
	OK                                             bool
}

type systemView struct {
	ID, Name, CoreID, CoreName, Expected, Provisioning, Platforms string
	Extensions, InputProfile, DisplayProfile, Mode                string
	Native                                                        bool
	Firmware                                                      []fwView
	Clients                                                       []clientView
}

type systemsBody struct{ Systems []systemView }

func statusView(f hub.FirmwareFile) (label, class string) {
	switch f.State {
	case hub.FirmwareValid:
		return "✓ Valid", "ok"
	case hub.FirmwareMismatch:
		return "✕ Hash mismatch", "error"
	case hub.FirmwareMissing:
		return "○ Missing", "error dashed"
	}
	return "○ Optional", ""
}

func (s *Server) systemsBodyData(r *http.Request) (systemsBody, error) {
	reg, err := s.svc.ListRegistry(r.Context())
	if err != nil {
		return systemsBody{}, err
	}
	var b systemsBody
	for _, e := range reg {
		v := systemView{ID: e.ID, Name: e.Name, CoreID: e.CoreID, CoreName: e.CoreName, Expected: e.ExpectedCoreVersion,
			Provisioning: e.Provisioning, Platforms: strings.Join(e.Platforms, ", "), Extensions: strings.Join(e.Extensions, ", "),
			InputProfile: e.InputProfile, DisplayProfile: e.DisplayProfile, Mode: string(e.FirmwareMode),
			Native: e.FirmwareMode == hub.FirmwareNative}
		for _, f := range e.Firmware {
			fv := fwView{ID: f.ID, Name: f.DisplayName, Requirement: "optional", Expected: "—", Present: "—", State: string(f.State),
				Pinned: f.Pinned != "", HasFile: f.Present, Sizes: strings.ReplaceAll(sizeList(f.Sizes), " or ", ", ")}
			if f.Required {
				fv.Requirement = "required"
			}
			if f.Pinned != "" {
				fv.PinFull = f.Pinned
				fv.Expected = shortHash(f.Pinned)
			}
			if f.Present {
				fv.Present = shortHash(f.SHA256) + " · " + humanBytes(f.Size)
			}
			fv.StateLabel, fv.Class = statusView(f)
			v.Firmware = append(v.Firmware, fv)
		}
		reports, err := s.svc.ListClientReports(r.Context(), e)
		if err != nil {
			return systemsBody{}, err
		}
		for _, c := range reports {
			cv := clientView{Device: c.DeviceName, Platform: c.Platform + "-" + c.Arch, Status: string(c.Status)}
			core := c.CoreVersion
			if core == "" {
				core = "not installed"
			}
			cv.Versions = "Player " + c.PlayerVersion + " · " + e.CoreName + " " + core
			switch c.Status {
			case hub.ClientCompatible:
				cv.OK, cv.StatusText = true, "compatible"
			case hub.ClientCoreMismatch:
				cv.StatusText = "Core version mismatch · " + c.Expected + " expected"
			case hub.ClientCoreMissing:
				cv.StatusText = "Core missing"
			case hub.ClientPlayerTooOld:
				cv.StatusText = "Player too old · protocol v" + c.Expected + " required"
			}
			v.Clients = append(v.Clients, cv)
		}
		b.Systems = append(b.Systems, v)
	}
	return b, nil
}

func sizeList(sizes []int64) string {
	parts := make([]string, len(sizes))
	for i, n := range sizes {
		parts[i] = humanBytes(n)
	}
	return strings.Join(parts, " or ")
}

func (s *Server) renderSystems(w http.ResponseWriter, r *http.Request, sess *session, status int, errMsg string) {
	body, err := s.systemsBodyData(r)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "systems", "Systems & Cores")
	d.Body = body
	if errMsg != "" {
		d.Error = errMsg
	}
	s.render(w, status, "systems", "layout", d)
}

func (s *Server) systemsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderSystems(w, r, sess, http.StatusOK, "")
}

// systemResult redirects on success and renders an error message for business errors.
func (s *Server) systemResult(w http.ResponseWriter, r *http.Request, sess *session, err error, ok string) {
	var he *hub.Error
	switch {
	case err == nil:
		http.Redirect(w, r, "/systems?ok="+ok, http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/systems?err=nofile", http.StatusSeeOther)
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		s.renderSystems(w, r, sess, http.StatusBadRequest, he.Message+".")
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) systemVersion(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetExpectedCoreVersion(r.Context(), r.PathValue("id"), r.PostFormValue("version")), "version")
}

func (s *Server) systemFirmwareMode(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetFirmwareMode(r.Context(), r.PathValue("id"), hub.FirmwareMode(r.PostFormValue("mode"))), "fwmode")
}

func (s *Server) firmwarePin(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetFirmwarePin(r.Context(), r.PathValue("id"), r.PathValue("file"), r.PostFormValue("sha256")), "fwpin")
}

func (s *Server) firmwareRemove(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.RemoveFirmware(r.Context(), r.PathValue("id"), r.PathValue("file")), "fwremoved")
}

// firmwareUpload takes a multipart form (_csrf, file). The body is bounded; the bytes are never logged.
func (s *Server) firmwareUpload(w http.ResponseWriter, r *http.Request, sess *session) {
	r.Body = http.MaxBytesReader(w, r.Body, maxFirmwareForm)
	if err := r.ParseMultipartForm(maxFirmwareForm); err != nil {
		s.renderSystems(w, r, sess, http.StatusBadRequest, "The file is too large or the form is invalid.")
		return
	}
	if r.Context().Value(keyCSRFChecked) != true && !hub.TokenEqual(r.PostFormValue("_csrf"), sess.CSRFToken) {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return
	}
	f, _, err := r.FormFile("file")
	if err != nil {
		s.renderSystems(w, r, sess, http.StatusBadRequest, "Please select a file.")
		return
	}
	defer f.Close()
	_, err = s.svc.ProvideFirmware(r.Context(), r.PathValue("id"), r.PathValue("file"), f)
	s.systemResult(w, r, sess, err, "fwfile")
}
