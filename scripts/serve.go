package main

import (
	"errors"
	"fmt"
	"log"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	addr    = ":8088"
	webRoot = "build/web/main/bin"
)

type loggingResponseWriter struct {
	http.ResponseWriter
	status int
	bytes  int64
}

func (w *loggingResponseWriter) WriteHeader(status int) {
	if w.status != 0 {
		return
	}
	w.status = status
	w.ResponseWriter.WriteHeader(status)
}

func (w *loggingResponseWriter) Write(data []byte) (int, error) {
	if w.status == 0 {
		w.WriteHeader(http.StatusOK)
	}
	n, err := w.ResponseWriter.Write(data)
	w.bytes += int64(n)
	return n, err
}

func main() {
	mime.AddExtensionType(".wasm", "application/wasm")
	mime.AddExtensionType(".js", "text/javascript")
	mime.AddExtensionType(".data", "application/octet-stream")

	handler := http.HandlerFunc(serveFile)

	log.SetOutput(os.Stderr)
	log.SetFlags(0)
	log.Printf("serving %s on http://localhost%s", webRoot, addr)

	if err := http.ListenAndServe(addr, logRequests(handler)); err != nil {
		log.Fatal(err)
	}
}

func logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		lrw := &loggingResponseWriter{ResponseWriter: w}

		next.ServeHTTP(lrw, r)

		status := lrw.status
		if status == 0 {
			status = http.StatusOK
		}

		log.Printf("%s %s %d %d %s", r.Method, r.URL.RequestURI(), status, lrw.bytes, time.Since(start))
	})
}

func serveFile(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")

	if r.Method == http.MethodOptions {
		w.Header().Set("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Range")
		w.WriteHeader(http.StatusNoContent)
		return
	}

	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	cleanPath := filepath.Clean("/" + r.URL.Path)
	if cleanPath == "/" {
		cleanPath = "/voxov_web.html"
	}

	relPath := strings.TrimPrefix(cleanPath, "/")
	fullPath := filepath.Join(webRoot, relPath)

	rootAbs, err := filepath.Abs(webRoot)
	if err != nil {
		http.Error(w, "internal server error", http.StatusInternalServerError)
		return
	}

	fullAbs, err := filepath.Abs(fullPath)
	if err != nil || !withinRoot(rootAbs, fullAbs) {
		http.NotFound(w, r)
		return
	}

	info, err := os.Stat(fullAbs)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			http.NotFound(w, r)
			return
		}
		http.Error(w, "internal server error", http.StatusInternalServerError)
		return
	}
	if info.IsDir() {
		http.NotFound(w, r)
		return
	}

	setHeaders(w, filepath.Ext(fullAbs))

	file, err := os.Open(fullAbs)
	if err != nil {
		http.Error(w, "internal server error", http.StatusInternalServerError)
		return
	}
	defer file.Close()

	http.ServeContent(w, r, filepath.Base(fullAbs), info.ModTime(), file)
}

func withinRoot(rootAbs, fullAbs string) bool {
	rel, err := filepath.Rel(rootAbs, fullAbs)
	if err != nil {
		return false
	}
	return rel == "." || (rel != ".." && !strings.HasPrefix(rel, fmt.Sprintf("..%c", os.PathSeparator)))
}

func setHeaders(w http.ResponseWriter, ext string) {
	if contentType := mime.TypeByExtension(ext); contentType != "" {
		w.Header().Set("Content-Type", contentType)
	}

	switch ext {
	case ".html":
		w.Header().Set("Cache-Control", "no-cache")
	case ".wasm", ".data":
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	}
}
