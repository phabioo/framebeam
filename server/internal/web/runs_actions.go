package web

import (
	"errors"
	"net/http"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Run actions of the 0.14 run detail (4jh-3): start (with or without the players who have not joined), finish,
// resend and remove open invitations. The admin acts on any run; the plugin content stays read-only.

func (s *Server) runStart(withoutInvited bool, ok string) handlerFunc {
	return func(w http.ResponseWriter, r *http.Request, sess *session) {
		id := r.PathValue("id")
		_, err := s.svc.StartRunAdmin(r.Context(), sess.User.ID, id, withoutInvited)
		s.runResult(w, r, id, ok, err)
	}
}

func (s *Server) runFinishConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	run, err := s.svc.GetRunAdmin(r.Context(), r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if run.Finished() {
		http.Redirect(w, r, "/runs/"+run.ID+"?err=runstate", http.StatusSeeOther)
		return
	}
	d := s.base(r, sess, "runs", "Finish run")
	d.Body = confirmBody{
		Heading: "Finish “" + run.Name + "”?",
		Intro:   "A finished run is over for all its participants. The following changes:",
		Removes: []string{
			"The run can no longer be started, paused or resumed",
			"The board and the event log stay readable; you can archive the run afterwards",
		},
		Note:   "Saves are not touched.",
		Action: "/runs/" + run.ID + "/finish", Button: "Finish run", Cancel: "/runs/" + run.ID,
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) runFinish(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	_, err := s.svc.SetRunStatusAdmin(r.Context(), sess.User.ID, id, hub.RunFinished)
	s.runResult(w, r, id, "runfinished", err)
}

// runResendInvite sends the invitation to one invited player again.
func (s *Server) runResendInvite(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	s.runResult(w, r, id, "invitesent", s.svc.ResendRunInvite(r.Context(), sess.User.ID, id, r.PathValue("uid")))
}

// runResendAll sends the invitation to everyone who has not joined yet (the banner button).
func (s *Server) runResendAll(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	s.runResult(w, r, id, "invitesent", s.svc.ResendRunInvites(r.Context(), sess.User.ID, id))
}

func (s *Server) runRemoveInviteConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	run, err := s.svc.GetRunAdmin(r.Context(), r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	p, ok := run.Participant(r.PathValue("uid"))
	if !ok || p.Status != hub.ParticipantInvited || run.Finished() {
		http.Redirect(w, r, "/runs/"+run.ID+"?err=runstate", http.StatusSeeOther)
		return
	}
	d := s.base(r, sess, "runs", "Remove invite")
	d.Body = confirmBody{
		Heading: "Remove the invite of " + p.UserName + "?",
		Intro:   p.UserName + " will no longer be part of “" + run.Name + "”. The following changes:",
		Removes: []string{
			p.UserName + " is removed from the participants",
			"Their app learns of it on its next sync",
		},
		Note:   "You can invite them to a run again from the app.",
		Action: "/runs/" + run.ID + "/invites/" + p.UserID + "/remove", Button: "Remove invite", Cancel: "/runs/" + run.ID,
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) runRemoveInvite(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	s.runResult(w, r, id, "inviteremoved", s.svc.RemoveRunInvite(r.Context(), sess.User.ID, id, r.PathValue("uid")))
}
