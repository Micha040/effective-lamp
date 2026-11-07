package main

import (
	"bufio"
	"bytes"
	"compress/bzip2"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/klauspost/compress/zstd"
)

// Fehler mit festem Kennwort, das die App in einen verständlichen Text übersetzt.
type codedError struct {
	code string
	err  error
}

func (e *codedError) Error() string { return e.code + ": " + e.err.Error() }

func coded(code string, format string, args ...any) error {
	return &codedError{code: code, err: fmt.Errorf(format, args...)}
}

func errorCode(err error) string {
	var ce *codedError
	if errors.As(err, &ce) {
		return ce.code
	}
	return "INTERNAL"
}

var (
	magicCS2  = []byte("PBDEMS2\x00")
	magicCSGO = []byte("HL2DEMO\x00")
	magicGzip = []byte{0x1f, 0x8b}
	magicBz2  = []byte("BZh")
	magicZstd = []byte{0x28, 0xb5, 0x2f, 0xfd}
)

// Größte entpackte Demo, die angenommen wird (schützt vor kaputten oder bösartigen Archiven).
const maxDemoSize = 4 << 30

// prepareDemo liefert den Pfad einer lesbaren .dem-Datei. Gepackte Demos (FACEIT liefert .dem.zst oder .dem.gz,
// Valve-Downloads .dem.bz2) werden in einen eigenen Unterordner von tempDir entpackt; cleanup räumt ihn wieder weg.
func prepareDemo(path, tempDir string) (demoPath, packed string, cleanup func(), err error) {
	cleanup = func() {}
	f, err := os.Open(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return "", "", cleanup, coded("NOT_FOUND", "%s", path)
		}
		return "", "", cleanup, coded("READ", "%v", err)
	}
	defer f.Close()

	head := make([]byte, 8)
	n, _ := io.ReadFull(f, head)
	head = head[:n]
	switch {
	case bytes.HasPrefix(head, magicCS2):
		return path, "", cleanup, nil
	case bytes.HasPrefix(head, magicCSGO):
		return "", "", cleanup, coded("CSGO", "CS:GO-Demos werden nicht unterstützt")
	case bytes.HasPrefix(head, magicGzip):
		packed = "gz"
	case bytes.HasPrefix(head, magicBz2):
		packed = "bz2"
	case bytes.HasPrefix(head, magicZstd):
		packed = "zst"
	default:
		return "", "", cleanup, coded("FORMAT", "keine CS2-Demo")
	}
	if _, err := f.Seek(0, io.SeekStart); err != nil {
		return "", "", cleanup, coded("READ", "%v", err)
	}

	var r io.Reader
	switch packed {
	case "gz":
		gz, err := gzip.NewReader(bufio.NewReaderSize(f, 1<<20))
		if err != nil {
			return "", "", cleanup, coded("FORMAT", "gzip: %v", err)
		}
		defer gz.Close()
		r = gz
	case "bz2":
		r = bzip2.NewReader(bufio.NewReaderSize(f, 1<<20))
	case "zst":
		zr, err := zstd.NewReader(bufio.NewReaderSize(f, 1<<20), zstd.WithDecoderConcurrency(1), zstd.WithDecoderLowmem(true))
		if err != nil {
			return "", "", cleanup, coded("FORMAT", "zstd: %v", err)
		}
		defer zr.Close()
		r = zr
	}

	if tempDir == "" {
		tempDir = os.TempDir()
	}
	dir, err := os.MkdirTemp(tempDir, "fragdesk-")
	if err != nil {
		return "", "", cleanup, coded("WRITE", "%v", err)
	}
	cleanup = func() { os.RemoveAll(dir) }

	// Den ursprünglichen Namen behalten: Die Quelle (FACEIT, ESEA, …) wird auch am Dateinamen erkannt.
	name := baseDemoName(filepath.Base(path))
	out := filepath.Join(dir, name)
	w, err := os.Create(out)
	if err != nil {
		cleanup()
		return "", "", func() {}, coded("WRITE", "%v", err)
	}
	bw := bufio.NewWriterSize(w, 1<<20)
	copied, err := io.Copy(bw, io.LimitReader(r, maxDemoSize+1))
	if err == nil {
		err = bw.Flush()
	}
	if cerr := w.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		cleanup()
		return "", "", func() {}, coded("UNPACK", "%v", err)
	}
	if copied > maxDemoSize {
		cleanup()
		return "", "", func() {}, coded("UNPACK", "entpackte Demo ist zu groß")
	}
	if err := checkMagic(out); err != nil {
		cleanup()
		return "", "", func() {}, err
	}

	// Datum erhalten: Ohne .info-Datei nimmt die Auswertung das Änderungsdatum als Spieldatum.
	if st, err := os.Stat(path); err == nil {
		_ = os.Chtimes(out, st.ModTime(), st.ModTime())
	}
	// Valve-Demos: die .info-Datei mit Datum und Share-Code liegt neben der Demo.
	for _, info := range []string{strings.TrimSuffix(path, filepath.Ext(path)) + ".info", path + ".info"} {
		if data, err := os.ReadFile(info); err == nil && len(data) < 1<<20 {
			_ = os.WriteFile(out+".info", data, 0o644)
			break
		}
	}
	return out, packed, cleanup, nil
}

func checkMagic(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return coded("READ", "%v", err)
	}
	defer f.Close()
	head := make([]byte, 8)
	if _, err := io.ReadFull(f, head); err != nil || !bytes.Equal(head, magicCS2) {
		if bytes.Equal(head, magicCSGO) {
			return coded("CSGO", "CS:GO-Demos werden nicht unterstützt")
		}
		return coded("FORMAT", "entpackte Datei ist keine CS2-Demo")
	}
	return nil
}

// "match.dem.zst" -> "match.dem", "match.zst" -> "match.dem"
func baseDemoName(name string) string {
	lower := strings.ToLower(name)
	for _, ext := range []string{".gz", ".bz2", ".zst"} {
		if strings.HasSuffix(lower, ext) {
			name = name[:len(name)-len(ext)]
			lower = lower[:len(lower)-len(ext)]
			break
		}
	}
	if !strings.HasSuffix(lower, ".dem") {
		name += ".dem"
	}
	if name == ".dem" {
		name = "demo.dem"
	}
	return name
}
