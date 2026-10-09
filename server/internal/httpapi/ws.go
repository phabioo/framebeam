package httpapi

import (
	"context"
	"net/http"
	"time"

	"github.com/coder/websocket"
)

// Transport settings of the WSS endpoint (variables so tests can shorten them).
var (
	wsPingInterval = 20 * time.Second // ADR 0006 D3
	wsHelloTimeout = 10 * time.Second
	wsPingTimeout  = 10 * time.Second
)

const wsReadLimit = 64 << 10 // SDP and candidates are small

// serveWS upgrades GET /api/v1/ws (Bearer access token in the upgrade request) and runs the Hub-side
// protocol (hub.Client) over the connection. It is registered directly on the mux because the upgrade
// hijacks the connection.
func (s *Server) serveWS(w http.ResponseWriter, r *http.Request) {
	p, err := s.svc.Authenticate(r.Context(), bearer(r))
	if err != nil {
		s.writeErr(w, r, err)
		return
	}
	conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{CompressionMode: websocket.CompressionDisabled})
	if err != nil {
		return // Accept already answered
	}
	conn.SetReadLimit(wsReadLimit)
	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()
	cl := s.svc.NewClient(p)
	cl.SetRequestHost(r.Host)
	cl.SetRemoteAddr(r.RemoteAddr)
	defer cl.Disconnect()

	go func() { // writer, keepalive, hub-initiated close
		defer cancel()
		ping := time.NewTicker(wsPingInterval)
		defer ping.Stop()
		hello := time.NewTimer(wsHelloTimeout)
		defer hello.Stop()
		write := func(b []byte) bool {
			wctx, wcancel := context.WithTimeout(ctx, wsPingTimeout)
			defer wcancel()
			return conn.Write(wctx, websocket.MessageText, b) == nil
		}
		for {
			select {
			case b := <-cl.Out():
				if !write(b) {
					conn.CloseNow()
					return
				}
			case <-cl.Done():
				for { // flush queued messages (e.g. the error that explains the close)
					select {
					case b := <-cl.Out():
						write(b)
						continue
					default:
					}
					break
				}
				code, why := cl.CloseInfo()
				conn.Close(websocket.StatusCode(code), why)
				return
			case <-ping.C:
				pctx, pcancel := context.WithTimeout(ctx, wsPingTimeout)
				err := conn.Ping(pctx)
				pcancel()
				if err != nil {
					conn.CloseNow()
					return
				}
			case <-hello.C:
				if !cl.Ready() {
					conn.Close(websocket.StatusPolicyViolation, "hello timeout")
					return
				}
			case <-ctx.Done():
				return
			}
		}
	}()

	for {
		typ, data, err := conn.Read(ctx)
		if err != nil {
			conn.CloseNow()
			return
		}
		if typ != websocket.MessageText {
			conn.Close(websocket.StatusUnsupportedData, "text frames only")
			return
		}
		if err := cl.Handle(data); err != nil {
			// Let the writer flush the explaining error message and close.
			cl.Fail()
			<-ctx.Done()
			return
		}
	}
}
