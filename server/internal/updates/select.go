package updates

import (
	"errors"
	"fmt"
)

// Errors of Select.
var (
	// ErrOff is returned for channel "dev" (or empty): the updater does not check.
	ErrOff = errors.New("updates are off (development build; select a channel to enable them)")
	// ErrCurrentNotSemVer is returned when the running version is not SemVer (e.g. Hub "dev").
	ErrCurrentNotSemVer = errors.New("the running version is not a release version; the updater is disabled")
)

// ChannelsFor returns the index channels a selected channel considers: beta sees beta and stable releases,
// stable only stable, anything else (dev, empty) nothing.
func ChannelsFor(channel string) []string {
	switch channel {
	case ChannelStable:
		return []string{ChannelStable}
	case ChannelBeta:
		return []string{ChannelBeta, ChannelStable}
	}
	return nil
}

// NormalizeChannel maps the legacy channel name "test" (old stored settings, old ldflags) to beta.
func NormalizeChannel(channel string) string {
	if channel == "test" {
		return ChannelBeta
	}
	return channel
}

// ValidSelectableChannel reports whether channel can be selected by a user (stable or beta).
func ValidSelectableChannel(channel string) bool {
	return channel == ChannelStable || channel == ChannelBeta
}

// Query selects a release.
type Query struct {
	Product  string
	Channel  string // the selected channel (stable or beta)
	Platform string // e.g. linux-amd64
	Kind     string // e.g. deb
	Current  string // running version (SemVer)
}

// Selection is the result of Select.
type Selection struct {
	// Release and Artifact are set when a newer release exists.
	Release  *Release
	Artifact *Artifact
	// UpToDate is true when no newer release exists (the running version is the highest or equal to it).
	UpToDate bool
}

// Select picks the highest release of the product in the channels of q.Channel that has an artifact for the
// platform and kind and is strictly newer than q.Current. It never selects a downgrade. Protocol compatibility
// is judged by the caller (ProtocolCompatible, BreaksPlayers), since it differs between Hub and Player.
func Select(idx Index, q Query) (Selection, error) {
	chans := ChannelsFor(q.Channel)
	if chans == nil {
		return Selection{}, ErrOff
	}
	cur, err := ParseSemVer(q.Current)
	if err != nil {
		return Selection{}, ErrCurrentNotSemVer
	}
	var best *Release
	var bestArt *Artifact
	var bestV SemVer
	for i := range idx.Releases {
		r := &idx.Releases[i]
		if r.Product != q.Product || !containsStr(chans, r.Channel) {
			continue
		}
		var art *Artifact
		for j := range r.Artifacts {
			if r.Artifacts[j].Platform == q.Platform && r.Artifacts[j].Kind == q.Kind {
				art = &r.Artifacts[j]
			}
		}
		if art == nil {
			continue
		}
		v, err := ParseSemVer(r.Version)
		if err != nil || v.Compare(cur) <= 0 {
			continue
		}
		if best == nil || v.Compare(bestV) > 0 {
			best, bestArt, bestV = r, art, v
		}
	}
	if best == nil {
		return Selection{UpToDate: true}, nil
	}
	rel, art := *best, *bestArt
	return Selection{Release: &rel, Artifact: &art}, nil
}

func containsStr(s []string, v string) bool {
	for _, x := range s {
		if x == v {
			return true
		}
	}
	return false
}

// ProtocolCompatible reports whether a candidate release and a counterpart (e.g. the Hub a Player talks to)
// can work together: candidate.protocol >= current.min && current.protocol >= candidate.min.
func ProtocolCompatible(candProto, candMin, curProto, curMin int) bool {
	return candProto >= curMin && curProto >= candMin
}

// BreaksPlayers reports whether installing a Hub release would lock out a Player: its min_protocol_version is
// above the protocol version of a Player seen recently.
func BreaksPlayers(candMinProtocol int, playerProtocols []int) bool {
	for _, p := range playerProtocols {
		if candMinProtocol > p {
			return true
		}
	}
	return false
}

// Describe returns a short text for logs.
func (s Selection) Describe() string {
	if s.Release == nil {
		return "up to date"
	}
	return fmt.Sprintf("%s (%s)", s.Release.Version, s.Release.Channel)
}
