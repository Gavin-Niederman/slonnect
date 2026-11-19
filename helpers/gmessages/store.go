package main

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"github.com/zalando/go-keyring"
)

const (
	keyringService = "slonnect-gmessages"
	keyringUser    = "session"
)

// Store keeps the paired session (Google cookies plus pairing keys: effectively the
// keys to the account) in the Secret Service, falling back to a 0600 file when no
// Secret Service is running.
type Store struct {
	ForceFile bool
	Backend   string // "keyring" | "file", whichever last worked
}

func stateDir() string {
	if d := os.Getenv("XDG_STATE_HOME"); d != "" {
		return filepath.Join(d, "slonnect", "gmessages")
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".local", "state", "slonnect", "gmessages")
}

func cacheDir(sub string) string {
	base, err := os.UserCacheDir()
	if err != nil {
		base = os.TempDir()
	}
	dir := filepath.Join(base, "slonnect", "gmessages", sub)
	_ = os.MkdirAll(dir, 0o700)
	return dir
}

// cachePath gives a stable file name for an ID that may contain anything.
func cachePath(sub, id, ext string) string {
	sum := sha256.Sum256([]byte(id))
	return filepath.Join(cacheDir(sub), hex.EncodeToString(sum[:12])+ext)
}

func (s *Store) sessionFile() string { return filepath.Join(stateDir(), "session.json") }

func (s *Store) Load() ([]byte, error) {
	if !s.ForceFile {
		if v, err := keyring.Get(keyringService, keyringUser); err == nil {
			s.Backend = "keyring"
			return []byte(v), nil
		} else if !errors.Is(err, keyring.ErrNotFound) {
			// No Secret Service (or it's locked): try the file.
		}
	}
	data, err := os.ReadFile(s.sessionFile())
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err == nil {
		s.Backend = "file"
	}
	return data, err
}

func (s *Store) Save(data []byte) error {
	if !s.ForceFile {
		if err := keyring.Set(keyringService, keyringUser, string(data)); err == nil {
			s.Backend = "keyring"
			_ = os.Remove(s.sessionFile()) // don't leave an older copy lying around
			return nil
		}
	}
	if err := os.MkdirAll(stateDir(), 0o700); err != nil {
		return err
	}
	tmp := s.sessionFile() + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	if err := os.Rename(tmp, s.sessionFile()); err != nil {
		return fmt.Errorf("saving session: %w", err)
	}
	s.Backend = "file"
	return nil
}

func (s *Store) Clear() {
	_ = keyring.Delete(keyringService, keyringUser)
	_ = os.Remove(s.sessionFile())
	_ = os.RemoveAll(filepath.Dir(cacheDir("")))
	s.Backend = ""
}
