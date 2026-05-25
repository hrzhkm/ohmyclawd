package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func newTestHandler(t *testing.T) (*Handler, *State, *Metrics) {
	t.Helper()
	s := NewState()
	m := NewMetrics()
	return NewHandler(s, m, nil, func() time.Time { return time.Unix(1747353660, 0) }, ""), s, m
}

func TestUsageHandler200(t *testing.T) {
	h, s, _ := newTestHandler(t)
	s.Store(&Usage{S: 50, W: 25, ST: "allowed", OK: true, Ts: 100})

	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/usage", nil))
	if rr.Code != 200 {
		t.Fatalf("status = %d", rr.Code)
	}
	if !strings.Contains(rr.Body.String(), `"s":50`) {
		t.Fatalf("body = %s", rr.Body.String())
	}
}

func TestHealthzHandler(t *testing.T) {
	h, _, _ := newTestHandler(t)
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/healthz", nil))
	if rr.Code != 200 || rr.Body.String() != "ok" {
		t.Fatalf("body=%q code=%d", rr.Body.String(), rr.Code)
	}
}

func TestUsageHandler_BlockedWithoutToken(t *testing.T) {
	s := NewState()
	m := NewMetrics()
	h := NewHandler(s, m, nil, time.Now, "mysecret")
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/usage", nil))
	if rr.Code != 401 {
		t.Fatalf("expected 401, got %d", rr.Code)
	}
}

func TestHealthz_PassesThroughWithToken(t *testing.T) {
	s := NewState()
	m := NewMetrics()
	h := NewHandler(s, m, nil, time.Now, "mysecret")
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/healthz", nil))
	if rr.Code != 200 {
		t.Fatalf("expected 200, got %d", rr.Code)
	}
}

func TestMetricsHandler(t *testing.T) {
	h, s, m := newTestHandler(t)
	s.Store(&Usage{Ts: 1747353600}) // 60 s ago given fixed Now
	m.IncProbe()
	m.IncFail()

	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/metrics", nil))
	if rr.Code != 200 {
		t.Fatalf("code = %d", rr.Code)
	}
	body := rr.Body.String()
	for _, want := range []string{
		"ohmyclawd_probe_total 1",
		"ohmyclawd_probe_fail_total 1",
		"ohmyclawd_probe_last_age_seconds 60",
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("missing %q in:\n%s", want, body)
		}
	}
}
