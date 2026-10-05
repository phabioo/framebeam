// Package config liest die Hub-Konfiguration aus Flags und FRAMEBEAM_*-Umgebungsvariablen.
package config

import (
	"errors"
	"flag"
	"net"
	"strconv"
)

// Config ist die Laufzeitkonfiguration des FrameBeam Hub.
type Config struct {
	DataDir string
	Listen  string
	// Name ist der Anzeigename beim ersten Start; danach gilt der in der Datenbank gespeicherte Name.
	Name    string
	TLSCert string
	TLSKey  string
	// Dev erlaubt HTTP statt HTTPS (nur für Entwicklung und Tests).
	Dev bool
}

// Register registriert die Konfigurationsflags auf fs. Vorbelegung: Env (FRAMEBEAM_*), sonst Default.
// Ein explizit gesetztes Flag überschreibt die Umgebung.
func Register(fs *flag.FlagSet, getenv func(string) string) *Config {
	c := &Config{}
	env := func(key, def string) string {
		if v := getenv("FRAMEBEAM_" + key); v != "" {
			return v
		}
		return def
	}
	dev, _ := strconv.ParseBool(getenv("FRAMEBEAM_DEV"))
	fs.StringVar(&c.DataDir, "data-dir", env("DATA_DIR", "/var/lib/framebeam"), "Datenverzeichnis (FRAMEBEAM_DATA_DIR)")
	fs.StringVar(&c.Listen, "listen", env("LISTEN", ":8443"), "Listen-Adresse (FRAMEBEAM_LISTEN)")
	fs.StringVar(&c.Name, "name", env("NAME", ""), "Hub-Name beim ersten Start (FRAMEBEAM_NAME)")
	fs.StringVar(&c.TLSCert, "tls-cert", env("TLS_CERT", ""), "TLS-Zertifikat (PEM) (FRAMEBEAM_TLS_CERT)")
	fs.StringVar(&c.TLSKey, "tls-key", env("TLS_KEY", ""), "TLS-Schlüssel (PEM) (FRAMEBEAM_TLS_KEY)")
	fs.BoolVar(&c.Dev, "dev", dev, "Entwicklungsmodus: HTTP statt HTTPS (FRAMEBEAM_DEV)")
	return c
}

// Validate prüft die Konfiguration auf Widersprüche.
func (c *Config) Validate() error {
	if c.DataDir == "" {
		return errors.New("data-dir darf nicht leer sein")
	}
	if (c.TLSCert == "") != (c.TLSKey == "") {
		return errors.New("tls-cert und tls-key nur gemeinsam angeben")
	}
	if c.Dev && c.TLSCert != "" {
		return errors.New("-dev (HTTP) und tls-cert/tls-key schließen sich aus")
	}
	return nil
}

// UseTLS meldet, ob der Hub HTTPS spricht. HTTP gibt es nur mit -dev.
func (c *Config) UseTLS() bool { return !c.Dev }

// ListenIsLoopback meldet, ob die Listen-Adresse ausschließlich Loopback bindet.
func (c *Config) ListenIsLoopback() bool {
	host, _, err := net.SplitHostPort(c.Listen)
	if err != nil || host == "" {
		return false
	}
	if host == "localhost" {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}
