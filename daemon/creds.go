package main

import (
	"encoding/json"
	"fmt"
	"os"
)

type Creds struct {
	AccessToken  string
	RefreshToken string
	ExpiresAt    int64 // milliseconds since epoch (matches Claude Code file)
	Scopes       []string
}

type credsFile struct {
	ClaudeAiOauth *struct {
		AccessToken  string   `json:"accessToken"`
		RefreshToken string   `json:"refreshToken"`
		ExpiresAt    int64    `json:"expiresAt"`
		Scopes       []string `json:"scopes"`
	} `json:"claudeAiOauth"`
}

// LoadCreds reads the Claude Code credentials file at path.
// Returns a wrapped os.ErrNotExist when the file is absent so callers can
// distinguish "not yet provisioned" from a parse error.
func LoadCreds(path string) (*Creds, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err // includes os.ErrNotExist on missing
	}
	var f credsFile
	if err := json.Unmarshal(data, &f); err != nil {
		return nil, fmt.Errorf("parse %s: %w", path, err)
	}
	if f.ClaudeAiOauth == nil || f.ClaudeAiOauth.AccessToken == "" {
		return nil, fmt.Errorf("%s: missing claudeAiOauth.accessToken", path)
	}
	return &Creds{
		AccessToken:  f.ClaudeAiOauth.AccessToken,
		RefreshToken: f.ClaudeAiOauth.RefreshToken,
		ExpiresAt:    f.ClaudeAiOauth.ExpiresAt,
		Scopes:       f.ClaudeAiOauth.Scopes,
	}, nil
}
