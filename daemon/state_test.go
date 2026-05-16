package main

import (
	"sync"
	"testing"
)

func TestStateStoreLoad(t *testing.T) {
	s := NewState()
	u := &Usage{S: 1, W: 2, ST: "allowed", OK: true, Ts: 100}
	s.Store(u)
	got, etag := s.Load()
	if got.S != 1 || got.W != 2 || !got.OK {
		t.Fatalf("got %+v", got)
	}
	if etag == "" {
		t.Fatal("etag empty")
	}
}

func TestStateETagStableAcrossEqualWrites(t *testing.T) {
	s := NewState()
	u1 := &Usage{S: 5, W: 6, ST: "allowed", OK: true, Ts: 100}
	u2 := &Usage{S: 5, W: 6, ST: "allowed", OK: true, Ts: 100}
	s.Store(u1)
	_, e1 := s.Load()
	s.Store(u2)
	_, e2 := s.Load()
	if e1 != e2 {
		t.Fatalf("etag changed across equal writes: %s -> %s", e1, e2)
	}
}

func TestStateETagChangesOnChange(t *testing.T) {
	s := NewState()
	s.Store(&Usage{S: 1})
	_, e1 := s.Load()
	s.Store(&Usage{S: 2})
	_, e2 := s.Load()
	if e1 == e2 {
		t.Fatal("etag did not change after Usage change")
	}
}

func TestStateConcurrentSafe(t *testing.T) {
	s := NewState()
	s.Store(&Usage{S: 0})
	var wg sync.WaitGroup
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func(v int) {
			defer wg.Done()
			s.Store(&Usage{S: v})
		}(i)
	}
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, _ = s.Load()
		}()
	}
	wg.Wait()
}
