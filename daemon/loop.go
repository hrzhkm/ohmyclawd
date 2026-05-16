package main

import (
	"context"
	"time"
)

type ProbeRunner interface {
	Run() (*Usage, error)
}

type LoopConfig struct {
	Base        time.Duration
	RateLimited time.Duration
	Backoff     []time.Duration
	Sleep       func(time.Duration) // injectable for tests; nil -> time.Sleep
	ReloadCreds func() (*Creds, error)
}

func RunLoop(ctx context.Context, p ProbeRunner, s *State, m *Metrics, cfg LoopConfig) {
	if cfg.Sleep == nil {
		cfg.Sleep = time.Sleep
	}
	idx := -1 // -1 means "use Base"
	for ctx.Err() == nil {
		m.IncProbe()
		u, err := p.Run()
		switch {
		case err == nil:
			s.Store(u)
			idx = -1
		case IsRateLimited(err):
			m.IncFail()
			if u != nil {
				s.Store(u)
			}
			cfg.Sleep(cfg.RateLimited)
			continue
		case IsAuthError(err):
			m.IncFail()
			if cfg.ReloadCreds != nil {
				if c, err2 := cfg.ReloadCreds(); err2 == nil && c.AccessToken != "" {
					if p2, ok := p.(*Prober); ok {
						p2.Token = c.AccessToken
					}
					// Single immediate retry with fresh token.
					u2, err3 := p.Run()
					if err3 == nil {
						s.Store(u2)
						idx = -1
						cfg.Sleep(cfg.Base)
						continue
					}
				}
			}
			// Still failing (or no ReloadCreds wired) -> back off like a normal error.
			if cur, _ := s.Load(); cur != nil {
				cur.OK = false
				cur.Ts = time.Now().Unix()
				s.Store(cur)
			}
			idx++
			if idx >= len(cfg.Backoff) {
				idx = len(cfg.Backoff) - 1
			}
			cfg.Sleep(cfg.Backoff[idx])
			continue
		default:
			m.IncFail()
			// keep last good Usage; flip OK=false copy
			if cur, _ := s.Load(); cur != nil {
				cur.OK = false
				cur.Ts = time.Now().Unix()
				s.Store(cur)
			}
			idx++
			if idx >= len(cfg.Backoff) {
				idx = len(cfg.Backoff) - 1
			}
			cfg.Sleep(cfg.Backoff[idx])
			continue
		}
		cfg.Sleep(cfg.Base)
	}
}
