package hostmonitor

import (
	"time"
	"github.com/shirou/gopsutil/v3/load"
	"github.com/shirou/gopsutil/v3/host"
)

var (
	quit chan bool
)


func StartSensors() {
	go func() {
		for {
			select {
			case <- quit:
				Log.Info("Stopping sensors")
				return
			case <- time.After(2 * time.Second):
				temps, err := GetTemp()
				if err != nil {
					Log.Error(err, "Error getting temp")
					continue
				}
				cpu, err := GetCPU()
				if err != nil {
					Log.Error(err, "Error getting CPU")
					continue
				}
				if (len(temps) > 0) {
					postUpdate(temps[0].Temperature, cpu)
				} else {
					postUpdate(0, cpu)
				}
			}
		}
	}()
}

func StopSensors() {
	Log.Info("Stopping sensors")
	quit <- true
}

func GetTemp() ([]host.TemperatureStat, error) {
	// Get the host info
	temps, err := host.SensorsTemperatures()
	if err != nil {
		Log.Error(err, "Error getting host info")
		return []host.TemperatureStat{}, err
	}
	return temps, nil
}

func GetCPU() (float64, error) {
	// Get the host info
	util, err := load.Avg()
	if err != nil {
		Log.Error(err, "Error getting CPU info")
		return 0, err
	}
	return util.Load1, nil
}