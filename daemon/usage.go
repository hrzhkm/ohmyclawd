package main

// Usage is the wire format served on /usage and consumed by the firmware.
// All fields are short keys to keep the payload tiny on the ESP32 side.
type Usage struct {
	S  int    `json:"s"`   // session-window utilization, 0..100
	SR int    `json:"sr"`  // minutes until session window resets
	W  int    `json:"w"`   // weekly-window utilization, 0..100
	WR int    `json:"wr"`  // minutes until weekly window resets
	ST string `json:"st"`  // "allowed" | "limited"
	OK bool   `json:"ok"`  // last probe succeeded
	Ts int64  `json:"ts"`  // unix seconds of last probe attempt
}
