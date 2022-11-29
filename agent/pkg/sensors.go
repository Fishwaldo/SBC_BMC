package hostmonitor

import (
	"path/filepath"
	"time"

	"github.com/shirou/gopsutil/v3/host"
	"github.com/shirou/gopsutil/v3/load"
	"github.com/spf13/viper"
)

var (
	quit chan bool
)


func StartSensors() {
	go func() {
		for {
			select {
			case <- quit:
				Log.Info("Stopping sensors Loop")
				return
			case <- time.After(time.Duration(viper.GetInt("agent.interval")) * time.Second):
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
				Log.Info("Sensor Readings", "temp", temps, "cpu", cpu)
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
	var ret []host.TemperatureStat
	for _, temp := range temps {
		match, _ := filepath.Match(viper.GetString("agent.tempsensor"), temp.SensorKey)
		if  match {
			ret = append(ret, temp)
			Log.Info("Adding Sensor Temp", "temp", temp.SensorKey)
		}
	}
	return ret, nil
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