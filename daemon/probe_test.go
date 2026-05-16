package main

import (
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func newProbeServer(t *testing.T, status int, headers map[string]string, body string) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			t.Errorf("method = %s, want POST", r.Method)
		}
		if got := r.Header.Get("Authorization"); got != "Bearer test-token" {
			t.Errorf("auth = %q", got)
		}
		if got := r.Header.Get("anthropic-version"); got != "2023-06-01" {
			t.Errorf("anthropic-version = %q", got)
		}
		for k, v := range headers {
			w.Header().Set(k, v)
		}
		w.WriteHeader(status)
		_, _ = w.Write([]byte(body))
	}))
}

func TestProbeHappyPath(t *testing.T) {
	resetEpoch := time.Now().Add(2 * time.Hour).Unix()
	weeklyEpoch := time.Now().Add(120 * time.Hour).Unix()
	srv := newProbeServer(t, 200, map[string]string{
		"anthropic-ratelimit-unified-5h-utilization": "0.45",
		"anthropic-ratelimit-unified-5h-reset":       intToStr(resetEpoch),
		"anthropic-ratelimit-unified-7d-utilization": "0.28",
		"anthropic-ratelimit-unified-7d-reset":       intToStr(weeklyEpoch),
		"anthropic-ratelimit-unified-status":         "allowed",
	}, `{"id":"msg_x","type":"message","content":[]}`)
	defer srv.Close()

	p := &Prober{URL: srv.URL, Token: "test-token", HTTP: srv.Client()}
	u, err := p.Run()
	if err != nil {
		t.Fatalf("probe err: %v", err)
	}
	if u.S != 45 || u.W != 28 || u.ST != "allowed" || !u.OK {
		t.Fatalf("usage = %+v", u)
	}
	if u.SR < 119 || u.SR > 121 {
		t.Fatalf("SR not ~120 minutes: %d", u.SR)
	}
}

func TestParseUtilization(t *testing.T) {
	cases := []struct {
		in   string
		want int
	}{
		{"0.0", 0},
		{"0.04", 4},
		{"0.88", 88},
		{"1.0", 100},
		{"88", 88},        // already-percent fallback (no dot)
		{"105", 100},      // already-percent fallback, clamped
		{"-1", 0},          // negative clamped
		{"", 0},            // empty -> 0
		{"not-a-number", 0},
	}
	for _, c := range cases {
		if got := parseUtilization(c.in); got != c.want {
			t.Errorf("parseUtilization(%q) = %d, want %d", c.in, got, c.want)
		}
	}
}

func TestProbeMissingHeaders(t *testing.T) {
	srv := newProbeServer(t, 200, map[string]string{}, `{}`)
	defer srv.Close()
	p := &Prober{URL: srv.URL, Token: "test-token", HTTP: srv.Client()}
	u, err := p.Run()
	if err == nil {
		t.Fatalf("expected error, got %+v", u)
	}
}

func TestProbe401(t *testing.T) {
	srv := newProbeServer(t, 401, nil, `{"error":{"type":"authentication_error"}}`)
	defer srv.Close()
	p := &Prober{URL: srv.URL, Token: "test-token", HTTP: srv.Client()}
	if _, err := p.Run(); err == nil || !IsAuthError(err) {
		t.Fatalf("expected auth error, got %v", err)
	}
}

func TestProbe429(t *testing.T) {
	srv := newProbeServer(t, 429, map[string]string{
		"anthropic-ratelimit-unified-5h-utilization": "100",
		"anthropic-ratelimit-unified-7d-utilization": "78",
		"anthropic-ratelimit-unified-status":         "limited",
	}, `{"error":{"type":"rate_limit_error"}}`)
	defer srv.Close()
	p := &Prober{URL: srv.URL, Token: "test-token", HTTP: srv.Client()}
	u, err := p.Run()
	if err == nil || !IsRateLimited(err) {
		t.Fatalf("expected rate-limit error, got %v", err)
	}
	if u == nil || u.ST != "limited" || u.S != 100 {
		t.Fatalf("usage on 429: %+v", u)
	}
}

func intToStr(n int64) string {
	// avoid strconv import in test helper file ordering; defined here for clarity.
	const digits = "0123456789"
	if n == 0 {
		return "0"
	}
	var buf [20]byte
	i := len(buf)
	for n > 0 {
		i--
		buf[i] = digits[n%10]
		n /= 10
	}
	return string(buf[i:])
}
