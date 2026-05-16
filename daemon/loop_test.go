package main

import (
	"context"
	"errors"
	"fmt"
	"sync/atomic"
	"testing"
	"time"
)

type fakeProber struct {
	calls   int
	results []struct {
		u   *Usage
		err error
	}
}

func (f *fakeProber) Run() (*Usage, error) {
	r := f.results[f.calls%len(f.results)]
	f.calls++
	return r.u, r.err
}

func TestLoopBackoffSchedule(t *testing.T) {
	fp := &fakeProber{results: []struct {
		u   *Usage
		err error
	}{
		{nil, errors.New("transient")},
		{nil, errors.New("transient")},
		{&Usage{S: 1, OK: true}, nil},
	}}
	intervals := []time.Duration{}
	var sleeps atomic.Int32
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	cfg := LoopConfig{
		Base:        1 * time.Millisecond,
		RateLimited: 5 * time.Millisecond,
		Backoff:     []time.Duration{1 * time.Millisecond, 2 * time.Millisecond, 4 * time.Millisecond},
		Sleep: func(d time.Duration) {
			intervals = append(intervals, d)
			// Cancel after we have observed all three iterations
			// (err -> Backoff[0], err -> Backoff[1], success -> Base).
			if sleeps.Add(1) >= 3 {
				cancel()
			}
		},
	}
	st := NewState()
	m := NewMetrics()
	RunLoop(ctx, fp, st, m, cfg)

	if fp.calls < 3 {
		t.Fatalf("calls = %d, want >=3", fp.calls)
	}
	if intervals[0] != 1*time.Millisecond || intervals[1] != 2*time.Millisecond {
		t.Fatalf("backoff schedule wrong: %v", intervals)
	}
	if intervals[2] != 1*time.Millisecond {
		t.Fatalf("Base sleep after success wrong: %v", intervals[2])
	}
	if u, _ := st.Load(); u.S != 1 || !u.OK {
		t.Fatalf("state not updated on success: %+v", u)
	}
	if m.probe.Load() < 3 || m.fail.Load() < 2 {
		t.Fatalf("metrics not updated: probe=%d fail=%d", m.probe.Load(), m.fail.Load())
	}
}

func TestLoopAuthRetryReloadsCreds(t *testing.T) {
	// First call returns 401 wrapped, second returns success.
	fp := &fakeProber{results: []struct {
		u   *Usage
		err error
	}{
		{nil, fmt.Errorf("status 401: %w", errAuth)},
		{&Usage{S: 7, OK: true}, nil},
	}}
	reloaded := atomic.Int32{}
	var sleeps atomic.Int32
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	cfg := LoopConfig{
		Base:        1 * time.Millisecond,
		RateLimited: 5 * time.Millisecond,
		Backoff:     []time.Duration{1 * time.Millisecond, 2 * time.Millisecond},
		ReloadCreds: func() (*Creds, error) {
			reloaded.Add(1)
			return &Creds{AccessToken: "refreshed-token"}, nil
		},
		Sleep: func(d time.Duration) {
			if sleeps.Add(1) >= 2 {
				cancel()
			}
		},
	}
	st := NewState()
	m := NewMetrics()
	RunLoop(ctx, fp, st, m, cfg)

	if reloaded.Load() < 1 {
		t.Fatalf("ReloadCreds not called on 401: %d", reloaded.Load())
	}
	if u, _ := st.Load(); u.S != 7 || !u.OK {
		t.Fatalf("post-retry state not stored: %+v", u)
	}
}
