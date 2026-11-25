package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
	"strings"
)

// Cookies Google Messages for Web needs from a signed-in Google session. The first
// six are required; __Secure-1PSIDTS is sometimes needed too.
var (
	wantedCookies   = []string{"SID", "HSID", "SSID", "OSID", "APISID", "SAPISID", "__Secure-1PSIDTS"}
	requiredCookies = []string{"SID", "HSID", "SSID", "OSID", "APISID", "SAPISID"}
)

// Cookie sources in a "Copy as cURL" command: Chrome and Firefox use -H 'cookie: …',
// newer Chrome uses -b '…'; Windows "cmd" copies use double quotes.
var curlCookieRes = []*regexp.Regexp{
	regexp.MustCompile(`(?i)(?:-H|--header)\s+\$?'cookie:\s*([^']*)'`),
	regexp.MustCompile(`(?i)(?:-H|--header)\s+"cookie:\s*((?:[^"\\]|\\.)*)"`),
	regexp.MustCompile(`(?i)(?:-b|--cookie)\s+\$?'([^']*)'`),
	regexp.MustCompile(`(?i)(?:-b|--cookie)\s+"((?:[^"\\]|\\.)*)"`),
}

// parseCookies accepts what people can copy out of a browser: a JSON object of
// cookie names to values, a "Copy as cURL" command, or a bare Cookie header.
func parseCookies(input string) (map[string]string, error) {
	input = strings.TrimSpace(input)
	if input == "" {
		return nil, errors.New("nothing pasted")
	}
	all := map[string]string{}
	switch {
	case strings.HasPrefix(input, "{"):
		if err := json.Unmarshal([]byte(input), &all); err != nil {
			return nil, fmt.Errorf("not a JSON object of cookie names to values: %w", err)
		}
	case strings.HasPrefix(strings.ToLower(input), "curl"):
		// Join line continuations so the regexes see one command.
		flat := strings.NewReplacer("\\\n", " ", "^\n", " ", "`\n", " ").Replace(input)
		found := false
		for _, re := range curlCookieRes {
			for _, m := range re.FindAllStringSubmatch(flat, -1) {
				addCookieHeader(all, m[1])
				found = true
			}
		}
		if !found {
			return nil, errors.New("the cURL command has no Cookie header; copy the request to messages.google.com/web/config")
		}
	default:
		addCookieHeader(all, strings.TrimPrefix(strings.TrimPrefix(input, "Cookie:"), "cookie:"))
	}

	out := map[string]string{}
	for _, name := range wantedCookies {
		if v, ok := all[name]; ok && v != "" {
			out[name] = v
		}
	}
	var missing []string
	for _, name := range requiredCookies {
		if out[name] == "" {
			missing = append(missing, name)
		}
	}
	if len(missing) > 0 {
		return nil, fmt.Errorf("missing cookies: %s (sign in at accounts.google.com in a private window first)", strings.Join(missing, ", "))
	}
	return out, nil
}

func addCookieHeader(into map[string]string, header string) {
	for _, part := range strings.Split(header, ";") {
		name, value, ok := strings.Cut(strings.TrimSpace(part), "=")
		if ok && name != "" {
			into[strings.TrimSpace(name)] = strings.TrimSpace(value)
		}
	}
}
