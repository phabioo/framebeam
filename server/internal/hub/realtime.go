package hub

import (
	"context"
	"encoding/json"
	"errors"
	"sync"
	"time"

	"github.com/google/uuid"
)

// Realtime layer (ADR 0006 D3): the Hub-side protocol of the WSS endpoint, independent of the WebSocket
// library. The transport (internal/httpapi) feeds incoming text frames to Client.Handle and writes
// Client.Out(); it closes the connection when Client.Done() fires.

// CloseCodePolicy is the WebSocket close code (1008, policy violation) used for hub-initiated closes.
const CloseCodePolicy = 1008

const outBuffer = 64

type sessionState struct {
	mu    sync.Mutex // serializes Session mutations; lock order: mu, then rt.mu
	ice   []string
	grace time.Duration

	rt rtState
}

type timerEntry struct {
	t        *time.Timer
	deviceID string
}

type rtState struct {
	mu           sync.Mutex
	conns        map[string]*Client    // device ID -> connection
	ownerTimers  map[string]timerEntry // session ID -> owner grace timer
	viewerTimers map[string]timerEntry // viewer ID -> "never connected" timer
}

func (ss *sessionState) init(o Options) {
	ss.ice = append([]string{}, o.ICEServers...)
	ss.grace = o.OwnerGrace
	if ss.grace <= 0 {
		ss.grace = DefaultOwnerGrace
	}
	ss.rt.conns = map[string]*Client{}
	ss.rt.ownerTimers = map[string]timerEntry{}
	ss.rt.viewerTimers = map[string]timerEntry{}
}

// recoverSessions runs on start: Sessions that were active before a restart survive for the grace period
// if their owner (and viewers) reconnect; otherwise they end like after a dropped connection.
func (s *Service) recoverSessions(ctx context.Context) {
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	ids, err := s.activeSessionIDs(ctx)
	if err != nil {
		return
	}
	for _, id := range ids {
		d, err := s.loadSession(ctx, id)
		if err != nil {
			continue
		}
		s.armOwnerGrace(d.id, d.ownerDeviceID)
		for _, v := range d.viewers {
			s.armViewerGrace(v.id, v.deviceID)
		}
	}
}

// ---- registry ----

func (s *Service) clients() []*Client {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	out := make([]*Client, 0, len(rt.conns))
	for _, c := range rt.conns {
		out = append(out, c)
	}
	return out
}

func (s *Service) clientOf(deviceID string) *Client {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	return rt.conns[deviceID]
}

func (s *Service) deviceConnected(deviceID string) bool { return s.clientOf(deviceID) != nil }

func (s *Service) userOnline(userID string) bool {
	for _, c := range s.clients() {
		if c.userID == userID {
			return true
		}
	}
	return false
}

// sendTo queues a message to a device if it is connected.
func (s *Service) sendTo(deviceID, typ string, payload any) {
	if c := s.clientOf(deviceID); c != nil {
		c.sendMsg(typ, "", payload)
	}
}

func (s *Service) armOwnerGrace(sessionID, deviceID string) {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	if old, ok := rt.ownerTimers[sessionID]; ok {
		old.t.Stop()
	}
	rt.ownerTimers[sessionID] = timerEntry{deviceID: deviceID,
		t: time.AfterFunc(s.sess.grace, func() { s.ownerGraceExpired(sessionID, deviceID) })}
}

func (s *Service) cancelSessionGrace(sessionID string) {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	if e, ok := rt.ownerTimers[sessionID]; ok {
		e.t.Stop()
		delete(rt.ownerTimers, sessionID)
	}
}

func (s *Service) ownerGraceExpired(sessionID, deviceID string) {
	ctx := context.Background()
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	rt := &s.sess.rt
	rt.mu.Lock()
	delete(rt.ownerTimers, sessionID)
	rt.mu.Unlock()
	if s.deviceConnected(deviceID) {
		return
	}
	if d, err := s.loadSession(ctx, sessionID); err == nil && !d.ended {
		s.endLocked(ctx, d, EndOwnerDisconnected)
	}
}

// armViewerGrace covers a viewer that joined by REST but never opened (or lost, over a Hub restart) its WSS
// connection: without a connection after the grace period the viewer is removed like after a drop.
func (s *Service) armViewerGrace(viewerID, deviceID string) {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	if old, ok := rt.viewerTimers[viewerID]; ok {
		old.t.Stop()
	}
	rt.viewerTimers[viewerID] = timerEntry{deviceID: deviceID,
		t: time.AfterFunc(s.sess.grace, func() { s.viewerGraceExpired(viewerID, deviceID) })}
}

func (s *Service) cancelViewerGrace(viewerID string) {
	rt := &s.sess.rt
	rt.mu.Lock()
	defer rt.mu.Unlock()
	if e, ok := rt.viewerTimers[viewerID]; ok {
		e.t.Stop()
		delete(rt.viewerTimers, viewerID)
	}
}

func (s *Service) viewerGraceExpired(viewerID, deviceID string) {
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	rt := &s.sess.rt
	rt.mu.Lock()
	delete(rt.viewerTimers, viewerID)
	rt.mu.Unlock()
	if s.deviceConnected(deviceID) {
		return
	}
	s.dropViewersOfDevice(context.Background(), deviceID, LeftDisconnected)
}

// ---- client ----

// Client is one WSS connection of a device.
type Client struct {
	svc      *Service
	userID   string
	deviceID string

	out  chan []byte
	done chan struct{}
	once sync.Once
	code int
	why  string

	mu         sync.Mutex
	ready      bool // hello processed, registered
	registered bool
	state      string
	gameID     string
}

// NewClient creates the connection state for an authenticated device. It is not visible to others until hello.
func (s *Service) NewClient(p Principal) *Client {
	return &Client{svc: s, userID: p.User.ID, deviceID: p.Device.ID, out: make(chan []byte, outBuffer), done: make(chan struct{}), state: "online"}
}

// Out delivers encoded messages (text frames) to write; Done fires when the connection must be closed.
func (c *Client) Out() <-chan []byte    { return c.out }
func (c *Client) Done() <-chan struct{} { return c.done }
func (c *Client) Ready() bool           { c.mu.Lock(); defer c.mu.Unlock(); return c.ready }

// Fail asks the transport to close the connection (policy violation) after queued messages are written.
func (c *Client) Fail() { c.closeWith(CloseCodePolicy, "protocol violation") }

// CloseInfo is the WebSocket close code and reason to use after Done fired.
func (c *Client) CloseInfo() (int, string) { return c.code, c.why }

func (c *Client) closeWith(code int, reason string) {
	c.once.Do(func() { c.code, c.why = code, reason; close(c.done) })
}

type envelope struct {
	Type    string          `json:"type"`
	ID      string          `json:"id,omitempty"`
	Payload json.RawMessage `json:"payload"`
}

func encode(typ, id string, payload any) []byte {
	var raw json.RawMessage
	switch v := payload.(type) {
	case json.RawMessage:
		raw = v
	default:
		raw, _ = json.Marshal(payload)
	}
	b, _ := json.Marshal(envelope{Type: typ, ID: id, Payload: raw})
	return b
}

func (c *Client) sendRaw(b []byte) {
	select {
	case c.out <- b:
	default:
		c.closeWith(CloseCodePolicy, "slow consumer")
	}
}

func (c *Client) sendMsg(typ, id string, payload any) { c.sendRaw(encode(typ, id, payload)) }

func (c *Client) sendError(id string, code Code, msg string) {
	c.sendMsg("error", id, map[string]string{"code": string(code), "message": msg})
}

var errFatal = errors.New("hub: connection must close")

// Handle processes one incoming text frame. A non-nil error means the connection must be closed
// (the reason was already sent to the client as error message where possible).
func (c *Client) Handle(data []byte) error {
	var env envelope
	if err := json.Unmarshal(data, &env); err != nil || env.Type == "" {
		c.sendError("", CodeBadRequest, "Invalid message")
		if !c.Ready() {
			return errFatal
		}
		return nil
	}
	if !c.Ready() {
		if env.Type != "hello" {
			c.sendError(env.ID, CodeBadRequest, "hello must be the first message")
			return errFatal
		}
		return c.hello(env)
	}
	switch env.Type {
	case "signal":
		c.signal(env)
	case "presence_update":
		c.presence(env)
	default:
		c.sendError(env.ID, CodeBadRequest, "Unknown message type")
	}
	return nil
}

func (c *Client) hello(env envelope) error {
	var h struct {
		ProtocolVersion int    `json:"protocol_version"`
		DeviceID        string `json:"device_id"`
	}
	if err := json.Unmarshal(env.Payload, &h); err != nil || h.ProtocolVersion < 1 {
		c.sendError(env.ID, CodeBadRequest, "hello needs protocol_version and device_id")
		return errFatal
	}
	if _, err := uuid.Parse(h.DeviceID); err != nil {
		c.sendError(env.ID, CodeBadRequest, "hello needs protocol_version and device_id")
		return errFatal
	}
	if h.DeviceID != c.deviceID {
		c.sendError(env.ID, CodeForbidden, "device_id does not match the access token")
		return errFatal
	}
	info := c.svc.Info()
	if h.ProtocolVersion < info.MinProtocolVersion {
		c.sendError(env.ID, "player_too_old", "Player protocol version below the hub's minimum")
		return errFatal
	}
	c.sendMsg("hello_ack", env.ID, map[string]any{"protocol_version": info.ProtocolVersion, "hub_version": info.HubVersion,
		"features": []string{FeatureSavesV1, FeatureSessionsV1}, "ice_servers": c.svc.ICEServers()})
	c.svc.attach(c)
	return nil
}

// attach registers the client (replacing an older connection of the same device), cancels grace timers of the
// device and sends the current presence and Session snapshot.
func (s *Service) attach(c *Client) {
	rt := &s.sess.rt
	rt.mu.Lock()
	old := rt.conns[c.deviceID]
	rt.conns[c.deviceID] = c
	for id, e := range rt.ownerTimers {
		if e.deviceID == c.deviceID {
			e.t.Stop()
			delete(rt.ownerTimers, id)
		}
	}
	for id, e := range rt.viewerTimers {
		if e.deviceID == c.deviceID {
			e.t.Stop()
			delete(rt.viewerTimers, id)
		}
	}
	rt.mu.Unlock()
	c.mu.Lock()
	c.ready, c.registered = true, true
	c.mu.Unlock()
	if old != nil {
		old.closeWith(CloseCodePolicy, "replaced by a newer connection")
	}
	for _, o := range s.clients() {
		if o != c {
			c.sendMsg("presence_update", "", o.presencePayload("")) // snapshot: who is where
			o.sendMsg("presence_update", "", c.presencePayload(""))
		}
	}
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	ctx := context.Background()
	ids, err := s.activeSessionIDs(ctx)
	if err != nil {
		return
	}
	for _, id := range ids {
		if d, err := s.loadSession(ctx, id); err == nil && d.allows(c.userID, c.deviceID) {
			c.sendMsg("session_update", "", map[string]any{"session": d.render(c.userID, c.deviceID, s.userOnline)})
		}
	}
}

func (c *Client) presencePayload(state string) map[string]string {
	c.mu.Lock()
	defer c.mu.Unlock()
	if state == "" {
		state = c.state
	}
	p := map[string]string{"user_id": c.userID, "device_id": c.deviceID, "state": state}
	if state == "in_game" && c.gameID != "" {
		p["game_id"] = c.gameID
	}
	return p
}

func (c *Client) presence(env envelope) {
	var p struct {
		State  string `json:"state"`
		GameID string `json:"game_id"`
	}
	if err := json.Unmarshal(env.Payload, &p); err != nil || (p.State != "online" && p.State != "in_game") {
		c.sendError(env.ID, CodeBadRequest, "state must be online or in_game")
		return
	}
	if p.GameID != "" {
		if _, err := uuid.Parse(p.GameID); err != nil || p.State != "in_game" {
			c.sendError(env.ID, CodeBadRequest, "game_id is only valid with state in_game and must be a UUID")
			return
		}
	}
	c.mu.Lock()
	c.state, c.gameID = p.State, p.GameID
	c.mu.Unlock()
	msg := encode("presence_update", "", c.presencePayload(""))
	for _, o := range c.svc.clients() {
		if o != c {
			o.sendRaw(msg)
		}
	}
}

// signal relays WebRTC signaling only between the owner device and the authorized viewer's device.
func (c *Client) signal(env envelope) {
	var sp struct {
		SessionID string `json:"session_id"`
		ViewerID  string `json:"viewer_id"`
		Kind      string `json:"kind"`
	}
	if err := json.Unmarshal(env.Payload, &sp); err != nil || sp.SessionID == "" || sp.ViewerID == "" ||
		(sp.Kind != "offer" && sp.Kind != "answer" && sp.Kind != "candidate") {
		c.sendError(env.ID, CodeBadRequest, "signal needs session_id, viewer_id and kind offer|answer|candidate")
		return
	}
	s := c.svc
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	forbidden := func() {
		c.sendError(env.ID, CodeSessionForbidden, "Signaling is not allowed for this Session or viewer")
	}
	d, err := s.loadSession(context.Background(), sp.SessionID)
	if err != nil || d.ended {
		forbidden()
		return
	}
	v := d.viewerByID(sp.ViewerID)
	if v == nil || !d.allows(v.userID, v.deviceID) {
		forbidden()
		return
	}
	var target string
	switch c.deviceID {
	case d.ownerDeviceID:
		target = v.deviceID
	case v.deviceID:
		target = d.ownerDeviceID
	default:
		forbidden()
		return
	}
	peer := s.clientOf(target)
	if peer == nil {
		c.sendError(env.ID, CodeNotFound, "Peer is not connected")
		return
	}
	peer.sendRaw(encode("signal", env.ID, env.Payload)) // the payload (SDP, candidates) is never parsed
}

// Disconnect must be called by the transport when the connection ended. A viewer whose connection drops is
// removed; a Session whose owner connection drops ends after the grace period unless the owner reconnects.
func (c *Client) Disconnect() {
	c.closeWith(CloseCodePolicy, "closed")
	c.mu.Lock()
	reg := c.registered
	c.mu.Unlock()
	if !reg {
		return
	}
	s := c.svc
	rt := &s.sess.rt
	rt.mu.Lock()
	current := rt.conns[c.deviceID] == c
	if current {
		delete(rt.conns, c.deviceID)
	}
	rt.mu.Unlock()
	if !current { // replaced by a newer connection: the device stays online
		return
	}
	ctx := context.Background()
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	msg := encode("presence_update", "", map[string]string{"user_id": c.userID, "device_id": c.deviceID, "state": "offline"})
	for _, o := range s.clients() {
		o.sendRaw(msg)
	}
	s.dropViewersOfDevice(ctx, c.deviceID, LeftDisconnected)
	if ids, err := s.activeSessionIDs(ctx, `AND owner_device_id = ?`, c.deviceID); err == nil {
		for _, id := range ids {
			s.armOwnerGrace(id, c.deviceID)
		}
	}
}
