package httpapi

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"
)

// A minimal JSON Schema checker for protocol/schemas (the subset these schemas use: type, const, enum,
// required, properties, items, minimum, maximum, minLength, pattern, format uuid/date-time, relative $ref).

const schemaDir = "../../../protocol/schemas"

func loadJSON(t testing.TB, path string) map[string]any {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatalf("%s: %v", path, err)
	}
	return m
}

func checkSchema(t testing.TB, schema map[string]any, v any, path string) error {
	if ref, ok := schema["$ref"].(string); ok {
		return checkSchema(t, loadJSON(t, filepath.Join(schemaDir, ref)), v, path)
	}
	if c, ok := schema["const"]; ok && c != v {
		return fmt.Errorf("%s: want const %v, got %v", path, c, v)
	}
	if en, ok := schema["enum"].([]any); ok {
		found := false
		for _, e := range en {
			found = found || e == v
		}
		if !found {
			return fmt.Errorf("%s: %v not in enum %v", path, v, en)
		}
	}
	switch schema["type"] {
	case "object":
		m, ok := v.(map[string]any)
		if !ok {
			return fmt.Errorf("%s: want object", path)
		}
		if req, ok := schema["required"].([]any); ok {
			for _, r := range req {
				if _, ok := m[r.(string)]; !ok {
					return fmt.Errorf("%s: missing required %q", path, r)
				}
			}
		}
		props, _ := schema["properties"].(map[string]any)
		for k, sub := range props {
			if val, ok := m[k]; ok {
				if err := checkSchema(t, sub.(map[string]any), val, path+"."+k); err != nil {
					return err
				}
			}
		}
	case "array":
		a, ok := v.([]any)
		if !ok {
			return fmt.Errorf("%s: want array (got %T)", path, v)
		}
		if items, ok := schema["items"].(map[string]any); ok {
			for i, x := range a {
				if err := checkSchema(t, items, x, fmt.Sprintf("%s[%d]", path, i)); err != nil {
					return err
				}
			}
		}
	case "string":
		str, ok := v.(string)
		if !ok {
			return fmt.Errorf("%s: want string", path)
		}
		if n, ok := schema["minLength"].(float64); ok && float64(len(str)) < n {
			return fmt.Errorf("%s: too short", path)
		}
		if p, ok := schema["pattern"].(string); ok && !regexp.MustCompile(p).MatchString(str) {
			return fmt.Errorf("%s: %q does not match %s", path, str, p)
		}
		switch schema["format"] {
		case "uuid":
			if _, err := uuid.Parse(str); err != nil {
				return fmt.Errorf("%s: not a UUID: %q", path, str)
			}
		case "date-time":
			if _, err := time.Parse(time.RFC3339, str); err != nil {
				return fmt.Errorf("%s: not a date-time: %q", path, str)
			}
		}
	case "integer":
		n, ok := v.(float64)
		if !ok || n != float64(int64(n)) {
			return fmt.Errorf("%s: want integer", path)
		}
		if min, ok := schema["minimum"].(float64); ok && n < min {
			return fmt.Errorf("%s: below minimum", path)
		}
		if max, ok := schema["maximum"].(float64); ok && n > max {
			return fmt.Errorf("%s: above maximum", path)
		}
	case "boolean":
		if _, ok := v.(bool); !ok {
			return fmt.Errorf("%s: want boolean", path)
		}
	}
	return nil
}

// validateWS checks one WSS message (raw JSON) against the envelope schema and the schema of its type.
func validateWS(t testing.TB, raw []byte) error {
	var v map[string]any
	if err := json.Unmarshal(raw, &v); err != nil {
		return err
	}
	if err := checkSchema(t, loadJSON(t, filepath.Join(schemaDir, "ws-envelope.schema.json")), v, "envelope"); err != nil {
		return err
	}
	typ, _ := v["type"].(string)
	file := filepath.Join(schemaDir, "ws-"+strings.ReplaceAll(typ, "_", "-")+".schema.json")
	if _, err := os.Stat(file); err != nil {
		return fmt.Errorf("no schema for message type %q", typ)
	}
	return checkSchema(t, loadJSON(t, file), v, typ)
}

func TestWSExamplesMatchSchemas(t *testing.T) {
	files, _ := filepath.Glob(filepath.Join(schemaDir, "examples", "ws-*.example.json"))
	if len(files) < 12 {
		t.Fatalf("only %d examples", len(files))
	}
	seen := map[string]bool{}
	for _, f := range files {
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		if err := validateWS(t, b); err != nil {
			t.Errorf("%s: %v", filepath.Base(f), err)
		}
		var v struct{ Type string }
		json.Unmarshal(b, &v)
		seen[v.Type] = true
	}
	for _, typ := range []string{"hello", "hello_ack", "presence_update", "session_update", "session_ended", "session_invite",
		"viewer_joined", "viewer_left", "signal", "error"} {
		if !seen[typ] {
			t.Errorf("no example for message type %s", typ)
		}
	}
}
