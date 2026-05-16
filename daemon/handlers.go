package main

import (
	"fmt"
	"net/http"
	"sync/atomic"
	"time"
)

type Metrics struct {
	probe atomic.Uint64
	fail  atomic.Uint64
}

func NewMetrics() *Metrics   { return &Metrics{} }
func (m *Metrics) IncProbe() { m.probe.Add(1) }
func (m *Metrics) IncFail()  { m.fail.Add(1) }

type Handler struct {
	state   *State
	metrics *Metrics
	now     func() time.Time
	mux     *http.ServeMux
}

func NewHandler(s *State, m *Metrics, now func() time.Time) *Handler {
	if now == nil {
		now = time.Now
	}
	h := &Handler{state: s, metrics: m, now: now, mux: http.NewServeMux()}
	h.mux.HandleFunc("/usage", h.usage)
	h.mux.HandleFunc("/healthz", h.healthz)
	h.mux.HandleFunc("/metrics", h.prom)
	return h
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) { h.mux.ServeHTTP(w, r) }

func (h *Handler) usage(w http.ResponseWriter, r *http.Request) {
	body, etag := h.state.Body()
	if etag != "" && r.Header.Get("If-None-Match") == etag {
		w.Header().Set("ETag", etag)
		w.WriteHeader(http.StatusNotModified)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("ETag", etag)
	w.Header().Set("Cache-Control", "no-store")
	_, _ = w.Write(body)
}

func (h *Handler) healthz(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/plain")
	_, _ = w.Write([]byte("ok"))
}

func (h *Handler) prom(w http.ResponseWriter, r *http.Request) {
	u, _ := h.state.Load()
	age := 0
	if u != nil && u.Ts > 0 {
		age = int(h.now().Unix() - u.Ts)
	}
	w.Header().Set("Content-Type", "text/plain; version=0.0.4")
	fmt.Fprintf(w, "# HELP ohmyclawd_probe_total Total probe attempts since start\n")
	fmt.Fprintf(w, "# TYPE ohmyclawd_probe_total counter\n")
	fmt.Fprintf(w, "ohmyclawd_probe_total %d\n", h.metrics.probe.Load())
	fmt.Fprintf(w, "# HELP ohmyclawd_probe_fail_total Probe failures since start\n")
	fmt.Fprintf(w, "# TYPE ohmyclawd_probe_fail_total counter\n")
	fmt.Fprintf(w, "ohmyclawd_probe_fail_total %d\n", h.metrics.fail.Load())
	fmt.Fprintf(w, "# HELP ohmyclawd_probe_last_age_seconds Seconds since last probe attempt\n")
	fmt.Fprintf(w, "# TYPE ohmyclawd_probe_last_age_seconds gauge\n")
	fmt.Fprintf(w, "ohmyclawd_probe_last_age_seconds %d\n", age)
}
