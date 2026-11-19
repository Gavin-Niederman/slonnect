package main

import "testing"

const fullHeader = "SID=sid1; HSID=h; SSID=s; OSID=o; APISID=a; SAPISID=sa; __Secure-1PSIDTS=ts; NID=ignored"

func TestParseCookies(t *testing.T) {
	cases := map[string]string{
		"json":        `{"SID":"sid1","HSID":"h","SSID":"s","OSID":"o","APISID":"a","SAPISID":"sa","__Secure-1PSIDTS":"ts","NID":"x"}`,
		"header":      "Cookie: " + fullHeader,
		"bare":        fullHeader,
		"chrome -H":   "curl 'https://messages.google.com/web/config' \\\n  -H 'accept: */*' \\\n  -H 'cookie: " + fullHeader + "' \\\n  -H 'user-agent: x'",
		"chrome -b":   "curl 'https://messages.google.com/web/config' \\\n  -b '" + fullHeader + "' \\\n  -H 'user-agent: x'",
		"firefox":     "curl 'https://messages.google.com/web/config' -H 'User-Agent: x' -H 'Cookie: " + fullHeader + "'",
		"windows cmd": `curl "https://messages.google.com/web/config" -H "cookie: ` + fullHeader + `"`,
		"bash $'' -b": "curl 'https://x' -b $'" + fullHeader + "'",
	}
	for name, in := range cases {
		got, err := parseCookies(in)
		if err != nil {
			t.Errorf("%s: %v", name, err)
			continue
		}
		if got["SID"] != "sid1" || got["__Secure-1PSIDTS"] != "ts" || len(got) != 7 {
			t.Errorf("%s: got %v", name, got)
		}
		if _, ok := got["NID"]; ok {
			t.Errorf("%s: kept a cookie we don't need", name)
		}
	}
}

func TestParseCookiesMissing(t *testing.T) {
	if _, err := parseCookies("SID=1; HSID=2"); err == nil {
		t.Fatal("expected an error for missing cookies")
	}
	if _, err := parseCookies("curl 'https://x' -H 'accept: */*'"); err == nil {
		t.Fatal("expected an error for a cURL command without cookies")
	}
}
