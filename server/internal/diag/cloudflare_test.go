package diag

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

const (
	testCFToken = "cfTokenSECRET0123456789abcdef"
	testCFKeyID = "cfkey-0123456789-full"
)

func cfBundleInfo(t *testing.T, status int) (map[string]any, string) {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if status != http.StatusOK {
			w.WriteHeader(status)
			return
		}
		_, _ = w.Write([]byte(`{"iceServers":[{"urls":["stun:stun.cloudflare.com:3478"]},` +
			`{"urls":["turn:turn.cloudflare.com:3478?transport=udp"],"username":"u","credential":"c"}]}`))
	}))
	defer srv.Close()
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartCloudflareTURN(ctx, hub.CloudflareTURNOptions{KeyID: testCFKeyID, Token: testCFToken, BaseURL: srv.URL, TTL: time.Hour,
		RetryMin: time.Hour})
	for i := 0; svc.CloudflareStatus().LastAttempt.IsZero(); i++ {
		if i > 400 {
			t.Fatal("no refresh attempt")
		}
		time.Sleep(10 * time.Millisecond)
	}
	o := Options{Config: config.Config{DataDir: t.TempDir()}, LogDir: t.TempDir(), NoJournal: true}
	info, err := Collect(context.Background(), svc, o)
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	if err := Write(context.Background(), &buf, info, o); err != nil {
		t.Fatal(err)
	}
	m := readZip(t, buf.Bytes())
	for name, c := range m {
		if strings.Contains(c, testCFToken) || strings.Contains(c, testCFKeyID) {
			t.Errorf("%s contains the Cloudflare token or full key id", name)
		}
	}
	var got map[string]any
	if err := json.Unmarshal([]byte(m[EntryInfo]), &got); err != nil {
		t.Fatal(err)
	}
	return got["network"].(map[string]any)["cloudflare_turn"].(map[string]any), m[EntryInfo]
}

func TestBundleCloudflareStatusOK(t *testing.T) {
	cf, _ := cfBundleInfo(t, http.StatusOK)
	if cf["configured"] != true || cf["last_result"] != "ok" || cf["key_id"] != "cfke..." || cf["last_refresh"] == "" {
		t.Errorf("cloudflare_turn: %v", cf)
	}
}

func TestBundleCloudflareStatusError(t *testing.T) {
	cf, _ := cfBundleInfo(t, http.StatusUnauthorized)
	if cf["configured"] != true || !strings.Contains(cf["last_result"].(string), "401") {
		t.Errorf("cloudflare_turn: %v", cf)
	}
}

func TestBundleCloudflareNotConfigured(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	info, err := Collect(context.Background(), svc, Options{Config: config.Config{DataDir: t.TempDir()}, NoJournal: true})
	if err != nil {
		t.Fatal(err)
	}
	if cf := info.Network.Cloudflare; cf == nil || cf.Configured || cf.KeyID != "" {
		t.Errorf("%+v", cf)
	}
}
