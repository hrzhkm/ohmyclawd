package main

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func dummyHandler() http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ok"))
	})
}

func TestAuthMiddleware_NoTokenConfigured_Passthrough(t *testing.T) {
	h := AuthMiddleware("", dummyHandler())
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/usage", nil))
	if rr.Code != 200 {
		t.Fatalf("expected 200, got %d", rr.Code)
	}
}

func TestAuthMiddleware_ValidToken(t *testing.T) {
	h := AuthMiddleware("secret123", dummyHandler())
	req := httptest.NewRequest(http.MethodGet, "/usage", nil)
	req.Header.Set("Authorization", "Bearer secret123")
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != 200 {
		t.Fatalf("expected 200, got %d", rr.Code)
	}
}

func TestAuthMiddleware_InvalidToken(t *testing.T) {
	h := AuthMiddleware("secret123", dummyHandler())
	req := httptest.NewRequest(http.MethodGet, "/usage", nil)
	req.Header.Set("Authorization", "Bearer wrong")
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != 401 {
		t.Fatalf("expected 401, got %d", rr.Code)
	}
}

func TestAuthMiddleware_MissingHeader(t *testing.T) {
	h := AuthMiddleware("secret123", dummyHandler())
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest(http.MethodGet, "/usage", nil))
	if rr.Code != 401 {
		t.Fatalf("expected 401, got %d", rr.Code)
	}
}
