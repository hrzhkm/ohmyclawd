package main

import (
	"encoding/json"
	"reflect"
	"testing"
)

func TestUsageJSONRoundTrip(t *testing.T) {
	in := Usage{S: 45, SR: 120, W: 28, WR: 7200, ST: "allowed", OK: true, Ts: 1747353600}
	b, err := json.Marshal(in)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	const want = `{"s":45,"sr":120,"w":28,"wr":7200,"st":"allowed","ok":true,"ts":1747353600}`
	if string(b) != want {
		t.Fatalf("got %s want %s", b, want)
	}
	var out Usage
	if err := json.Unmarshal(b, &out); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if !reflect.DeepEqual(in, out) {
		t.Fatalf("round-trip mismatch: %#v != %#v", in, out)
	}
}
