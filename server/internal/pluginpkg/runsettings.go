package pluginpkg

import (
	"archive/zip"
	"encoding/json"
	"fmt"
	"io"
	"regexp"
	"sort"
	"strings"
)

// Limits of the manifest field "run_settings" (identical in the Hub and the Player).
const (
	MaxRunSettings       = 32
	MinRunSettingOptions = 2
	MaxRunSettingOptions = 8
	maxI18nFileBytes     = 1 << 20
)

var (
	runKeySegRe  = regexp.MustCompile(`^[a-z][a-z0-9_]{0,31}$`)
	i18nKeyRe    = regexp.MustCompile(`^[A-Za-z0-9_.-]{1,64}$`)
	i18nLangRe   = regexp.MustCompile(`^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$`)
	runOptValRe  = runKeySegRe
	runSettingTy = map[string]bool{"boolean": true, "enum": true}
)

// RunSettingOption is one choice of an enum run setting.
type RunSettingOption struct {
	Value string `json:"value"`
	Label string `json:"label"` // i18n key
}

// RunSetting is one entry of the manifest field "run_settings": a rule the Player shows in the "Start a run" dialog.
// Label, Hint and the option labels are i18n keys (resolved from i18n/<lang>.json). Default is a bool for type
// "boolean" and one of the option values (string) for type "enum".
type RunSetting struct {
	Key     string             `json:"key"`
	Type    string             `json:"type"`
	Label   string             `json:"label"`
	Hint    string             `json:"hint,omitempty"`
	Default any                `json:"default"`
	Options []RunSettingOption `json:"options,omitempty"`
}

// parseRunSettings validates the (non-null) value of "run_settings". Unknown fields in an entry are ignored.
func parseRunSettings(raw json.RawMessage) ([]RunSetting, error) {
	var items []json.RawMessage
	if json.Unmarshal(raw, &items) != nil {
		return nil, invalid("manifest field 'run_settings' must be an array")
	}
	if len(items) < 1 || len(items) > MaxRunSettings {
		return nil, invalid("manifest field 'run_settings' must have 1 to %d entries", MaxRunSettings)
	}
	out := make([]RunSetting, 0, len(items))
	keys := map[string]int{}
	for i, it := range items {
		at := fmt.Sprintf("run_settings[%d]", i)
		var e struct {
			Key     json.RawMessage `json:"key"`
			Type    json.RawMessage `json:"type"`
			Label   json.RawMessage `json:"label"`
			Hint    json.RawMessage `json:"hint"`
			Default json.RawMessage `json:"default"`
			Options json.RawMessage `json:"options"`
		}
		if json.Unmarshal(it, &e) != nil || strings.TrimSpace(string(it))[0] != '{' {
			return nil, invalid("%s must be an object", at)
		}
		var s RunSetting
		if json.Unmarshal(e.Key, &s.Key) != nil || s.Key == "" {
			return nil, invalid("%s.key must be a string", at)
		}
		segs := strings.Split(s.Key, ".")
		if len(segs) > 2 {
			return nil, invalid("%s.key %q has more than 2 segments", at, s.Key)
		}
		for _, seg := range segs {
			if !runKeySegRe.MatchString(seg) {
				return nil, invalid("%s.key %q: each segment must match [a-z][a-z0-9_]{0,31}", at, s.Key)
			}
		}
		if j, dup := keys[s.Key]; dup {
			return nil, invalid("%s.key %q duplicates run_settings[%d]", at, s.Key, j)
		}
		keys[s.Key] = i
		if json.Unmarshal(e.Type, &s.Type) != nil || !runSettingTy[s.Type] {
			return nil, invalid(`%s.type must be "boolean" or "enum"`, at)
		}
		i18n := func(field string, raw json.RawMessage, required bool, dst *string) error {
			if len(raw) == 0 || string(raw) == "null" {
				if required {
					return invalid("%s.%s is required", at, field)
				}
				return nil
			}
			if json.Unmarshal(raw, dst) != nil || !i18nKeyRe.MatchString(*dst) {
				return invalid("%s.%s must be an i18n key matching [A-Za-z0-9_.-]{1,64}", at, field)
			}
			return nil
		}
		if err := i18n("label", e.Label, true, &s.Label); err != nil {
			return nil, err
		}
		if err := i18n("hint", e.Hint, false, &s.Hint); err != nil {
			return nil, err
		}
		hasOpts := len(e.Options) > 0 && string(e.Options) != "null"
		if len(e.Default) == 0 || string(e.Default) == "null" {
			return nil, invalid("%s.default is required", at)
		}
		switch s.Type {
		case "boolean":
			if hasOpts {
				return nil, invalid("%s.options is only allowed for type enum", at)
			}
			var b bool
			if json.Unmarshal(e.Default, &b) != nil {
				return nil, invalid("%s.default must be a boolean", at)
			}
			s.Default = b
		case "enum":
			if !hasOpts {
				return nil, invalid("%s.options is required for type enum", at)
			}
			var opts []json.RawMessage
			if json.Unmarshal(e.Options, &opts) != nil {
				return nil, invalid("%s.options must be an array", at)
			}
			if len(opts) < MinRunSettingOptions || len(opts) > MaxRunSettingOptions {
				return nil, invalid("%s.options must have %d to %d entries", at, MinRunSettingOptions, MaxRunSettingOptions)
			}
			seen := map[string]bool{}
			for j, ov := range opts {
				oat := fmt.Sprintf("%s.options[%d]", at, j)
				var o struct {
					Value json.RawMessage `json:"value"`
					Label json.RawMessage `json:"label"`
				}
				if json.Unmarshal(ov, &o) != nil || strings.TrimSpace(string(ov))[0] != '{' {
					return nil, invalid("%s must be an object", oat)
				}
				var opt RunSettingOption
				if json.Unmarshal(o.Value, &opt.Value) != nil || !runOptValRe.MatchString(opt.Value) {
					return nil, invalid("%s.value must match [a-z][a-z0-9_]{0,31}", oat)
				}
				if seen[opt.Value] {
					return nil, invalid("%s.value %q is not unique", oat, opt.Value)
				}
				seen[opt.Value] = true
				if len(o.Label) == 0 || string(o.Label) == "null" {
					return nil, invalid("%s.label is required", oat)
				}
				if json.Unmarshal(o.Label, &opt.Label) != nil || !i18nKeyRe.MatchString(opt.Label) {
					return nil, invalid("%s.label must be an i18n key matching [A-Za-z0-9_.-]{1,64}", oat)
				}
				s.Options = append(s.Options, opt)
			}
			var d string
			if json.Unmarshal(e.Default, &d) != nil || !seen[d] {
				return nil, invalid("%s.default must be one of the option values", at)
			}
			s.Default = d
		}
		out = append(out, s)
	}
	// No key may be a segment prefix of another ("rules" together with "rules.x").
	for k := range keys {
		if i := strings.IndexByte(k, '.'); i > 0 {
			if _, clash := keys[k[:i]]; clash {
				return nil, invalid("run_settings key %q conflicts with %q (a key cannot be the prefix of another)", k[:i], k)
			}
		}
	}
	return out, nil
}

// I18nKeys returns the i18n keys the run settings reference (labels, hints, option labels), sorted and unique.
func I18nKeys(rs []RunSetting) []string {
	set := map[string]bool{}
	for _, s := range rs {
		set[s.Label] = true
		if s.Hint != "" {
			set[s.Hint] = true
		}
		for _, o := range s.Options {
			set[o.Label] = true
		}
	}
	out := make([]string, 0, len(set))
	for k := range set {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

// RunSettingsStrings reads every i18n/<lang>.json of the package and returns, per language, the texts of the keys
// the manifest's run_settings reference (nested objects are addressed as "a.b"; non-string values are ignored; a
// key missing in a language is absent). Nil when the manifest has no run_settings. Files that are not valid JSON
// objects are skipped.
func (p *Package) RunSettingsStrings() map[string]map[string]string {
	if len(p.Manifest.RunSettings) == 0 {
		return nil
	}
	want := I18nKeys(p.Manifest.RunSettings)
	out := map[string]map[string]string{}
	for _, name := range p.Files {
		lang, ok := strings.CutPrefix(name, "i18n/")
		if !ok {
			continue
		}
		lang, ok = strings.CutSuffix(lang, ".json")
		if !ok || !i18nLangRe.MatchString(lang) {
			continue
		}
		f := p.entries[name]
		if f == nil || f.UncompressedSize64 > maxI18nFileBytes {
			continue
		}
		flat, ok := readFlatStrings(f)
		if !ok {
			continue
		}
		m := map[string]string{}
		for _, k := range want {
			if v, ok := flat[k]; ok {
				m[k] = v
			}
		}
		out[lang] = m
	}
	return out
}

func readFlatStrings(f *zip.File) (map[string]string, bool) {
	rc, err := f.Open()
	if err != nil {
		return nil, false
	}
	defer rc.Close()
	b, err := io.ReadAll(io.LimitReader(rc, maxI18nFileBytes+1))
	if err != nil || len(b) > maxI18nFileBytes {
		return nil, false
	}
	var root map[string]any
	if json.Unmarshal(b, &root) != nil || root == nil {
		return nil, false
	}
	flat := map[string]string{}
	var walk func(prefix string, m map[string]any, depth int)
	walk = func(prefix string, m map[string]any, depth int) {
		for k, v := range m {
			key := prefix + k
			switch t := v.(type) {
			case string:
				flat[key] = t
			case map[string]any:
				if depth < 8 {
					walk(key+".", t, depth+1)
				}
			}
		}
	}
	walk("", root, 0)
	return flat, true
}
