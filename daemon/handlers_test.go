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
	return NewHandler(s, m, func() time.Time { return time.Unix(1747353660, 0) }), s, m
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
	if rr.Header().Get("ETag") == "" {
		t.Fatal("missing ETag header")
	}
}

func TestUsageHandler304(t *testing.T) {
	h, s, _ := newTestHandler(t)
	s.Store(&Usage{S: 50})
	_, etag := s.Load()

	req := httptest.NewRequest(http.MethodGet, "/usage", nil)
	req.Header.Set("If-None-Match", etag)
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusNotModified {
		t.Fatalf("status = %d, want 304", rr.Code)
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
		"ohmycc_probe_total 1",
		"ohmycc_probe_fail_total 1",
		"ohmycc_probe_last_age_seconds 60",
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("missing %q in:\n%s", want, body)
		}
	}
}
