package main

import (
	"errors"
	"os"
	"testing"
)

func TestLoadCredsValid(t *testing.T) {
	c, err := LoadCreds("testdata/creds_valid.json")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if c.AccessToken != "sk-ant-oat01-VALIDTOKEN" {
		t.Fatalf("AccessToken = %q", c.AccessToken)
	}
	if c.ExpiresAt != 9999999999000 {
		t.Fatalf("ExpiresAt = %d", c.ExpiresAt)
	}
}

func TestLoadCredsMissingFile(t *testing.T) {
	_, err := LoadCreds("testdata/does_not_exist.json")
	if !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("expected ErrNotExist, got %v", err)
	}
}

func TestLoadCredsMalformed(t *testing.T) {
	_, err := LoadCreds("testdata/creds_bogus.json")
	if err == nil {
		t.Fatal("expected error for missing claudeAiOauth")
	}
}
