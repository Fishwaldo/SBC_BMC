package hostmonitor

import (
	stdlog "log"
	"os"

	"github.com/go-logr/logr"
	"github.com/go-logr/stdr"
)

var (
	Log logr.Logger
)

func init() {
	stdr.SetVerbosity(1)
	Log = stdr.NewWithOptions(stdlog.New(os.Stderr, "", stdlog.LstdFlags), stdr.Options{LogCaller: stdr.Error})
}