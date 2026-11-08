// fdanalyze wertet eine CS2-Demo für FragDesk aus (Teil von efficient lamp).
//
//	fdanalyze -demo <Datei> -out <Datei.json> [-replay <Datei.replay>] [-temp <Ordner>] [-pretty]
//	        (schreibt daneben <Datei>.heat mit den Heatmap-Daten, siehe extras.go; mit -replay im selben Lauf auch die
//	        Daten der 2D-Ansicht – fehlen die am Ende, ist die Auswertung trotzdem gültig, Hinweis unter "warnings")
//	fdanalyze -demo <Datei> -unpack <Datei.dem> [-temp <Ordner>]   (nur entpacken, z. B. zum Abspielen in CS2)
//	fdanalyze -demo <Datei> -replay <Datei.replay> [-temp <Ordner>]   (nur die Daten für die 2D-Ansicht, siehe replay.go)
//	fdanalyze -version
//
// Fortschritt und Ergebnis stehen zeilenweise auf der Standardausgabe:
//
//	STEP unpack|parse|stats|write
//	DONE <Datei.json>
//	ERROR <Kennwort> <Text>     (Kennwörter: NOT_FOUND, READ, WRITE, FORMAT, CSGO, UNPACK, POV, SOURCE, CORRUPT, EMPTY, INTERNAL)
//
// Die eigentliche Auswertung übernimmt cs-demo-analyzer von AkiVer (MIT-Lizenz).
package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

const (
	version     = "1.5.0"
	csdaVersion = "1.13.0"
)

func main() {
	demo := flag.String("demo", "", "Demo-Datei (.dem, .dem.gz, .dem.bz2, .dem.zst)")
	outPath := flag.String("out", "", "Ausgabedatei (JSON)")
	tempDir := flag.String("temp", "", "Ordner für entpackte Demos (Standard: TEMP)")
	pretty := flag.Bool("pretty", false, "JSON eingerückt schreiben")
	unpackTo := flag.String("unpack", "", "Demo nur entpacken und als diese .dem-Datei speichern")
	replayTo := flag.String("replay", "", "Daten für die 2D-Ansicht (Positionen, Granaten, Kills) in diese Datei schreiben")
	showVersion := flag.Bool("version", false, "Version ausgeben")
	flag.Parse()

	if *showVersion {
		fmt.Printf("fdanalyze %s (cs-demo-analyzer %s)\n", version, csdaVersion)
		return
	}
	if *demo != "" && *unpackTo != "" {
		stdout := bufio.NewWriter(os.Stdout)
		if err := unpackDemo(*demo, *unpackTo, *tempDir); err != nil {
			fmt.Fprintf(stdout, "ERROR %s %s\n", errorCode(err), strings.ReplaceAll(err.Error(), "\n", " "))
			stdout.Flush()
			os.Exit(1)
		}
		fmt.Fprintf(stdout, "DONE %s\n", *unpackTo)
		stdout.Flush()
		return
	}
	if *demo != "" && *replayTo != "" && *outPath == "" { // nur 2D-Daten
		stdout := bufio.NewWriter(os.Stdout)
		if err := replayFile(*demo, *replayTo, *tempDir); err != nil {
			fmt.Fprintf(stdout, "ERROR %s %s\n", errorCode(err), strings.ReplaceAll(err.Error(), "\n", " "))
			stdout.Flush()
			os.Exit(1)
		}
		fmt.Fprintf(stdout, "DONE %s\n", *replayTo)
		stdout.Flush()
		return
	}
	if *demo == "" || *outPath == "" {
		fmt.Fprintln(os.Stderr, "Aufruf: fdanalyze -demo <Datei> -out <Datei.json>")
		os.Exit(2)
	}

	stdout := bufio.NewWriter(os.Stdout)
	step := func(s string) {
		fmt.Fprintf(stdout, "STEP %s\n", s)
		stdout.Flush()
	}
	fail := func(err error) {
		msg := strings.ReplaceAll(err.Error(), "\n", " ")
		fmt.Fprintf(stdout, "ERROR %s %s\n", errorCode(err), msg)
		stdout.Flush()
		os.Exit(1)
	}

	out, err := analyzeFile(*demo, *tempDir, *replayTo, step)
	if err != nil {
		fail(err)
	}
	step("write")
	if err := writeJSON(*outPath, out, *pretty); err != nil {
		fail(coded("WRITE", "%v", err))
	}
	if len(out.heat) > 0 { // ohne Heatmap-Daten bleibt die Auswertung trotzdem gültig
		if err := writeHeat(heatPath(*outPath), out.heat); err != nil {
			fmt.Fprintf(os.Stderr, "Heatmap-Daten nicht geschrieben: %v\n", err)
		}
	}
	fmt.Fprintf(stdout, "DONE %s\n", *outPath)
	stdout.Flush()
}

// heatPath: Heatmap-Datei neben der Ausgabe (x.json -> x.heat)
func heatPath(out string) string { return strings.TrimSuffix(out, filepath.Ext(out)) + ".heat" }

// unpackDemo legt die Demo entpackt unter target ab (ungepackte Demos werden kopiert).
func unpackDemo(demo, target, tempDir string) error {
	if tempDir == "" {
		tempDir = filepath.Dir(target)
	}
	for _, dir := range []string{filepath.Dir(target), tempDir} {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return coded("WRITE", "%v", err)
		}
	}
	path, packed, cleanup, err := prepareDemo(demo, tempDir)
	if err != nil {
		return err
	}
	defer cleanup()
	if packed != "" && os.Rename(path, target) == nil {
		return nil
	}
	in, err := os.Open(path)
	if err != nil {
		return coded("READ", "%v", err)
	}
	defer in.Close()
	tmp := target + ".part"
	out, err := os.Create(tmp)
	if err != nil {
		return coded("WRITE", "%v", err)
	}
	_, err = io.Copy(out, in)
	if cerr := out.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp, target)
	}
	if err != nil {
		os.Remove(tmp)
		return coded("WRITE", "%v", err)
	}
	return nil
}

// Erst in eine Zwischendatei schreiben und dann umbenennen: Die App sieht nie eine halbe Datei.
func writeJSON(path string, v any, pretty bool) error {
	if dir := filepath.Dir(path); dir != "" {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	tmp := path + ".part"
	f, err := os.Create(tmp)
	if err != nil {
		return err
	}
	w := bufio.NewWriterSize(f, 1<<20)
	enc := json.NewEncoder(w)
	enc.SetEscapeHTML(false)
	if pretty {
		enc.SetIndent("", " ")
	}
	err = enc.Encode(v)
	if err == nil {
		err = w.Flush()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, path)
}
