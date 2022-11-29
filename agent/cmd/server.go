/*
Copyright © 2022 NAME HERE <EMAIL ADDRESS>

*/
package cmd

import (
	"os"
	"os/signal"
	"syscall"

	"github.com/Fishwaldo/esp32-sbcfanctrl/client/pkg"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

// agentCmd represents the server command
var agentCmd = &cobra.Command{
	Use:   "agent",
	Short: "Start the Agent Service",
	Long: `Starts the Agent to report CPU and Load Information to the SBC Controller`,
	Run: func(cmd *cobra.Command, args []string) {
		startAgent()
	},
}

var cfgFile string

func init() {
	cobra.OnInitialize(initConfig)
	rootCmd.AddCommand(agentCmd)

	rootCmd.PersistentFlags().BoolP("debug", "d", false, "Enable debug logging")
	viper.BindPFlag("debug", rootCmd.PersistentFlags().Lookup("debug"))
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "", "config file (default is /etc/sbcbmc_agent.yml)")
	agentCmd.PersistentFlags().StringP("server", "s", "localhost", "Server to connect to")
	viper.BindPFlag("sbcbmc.server", agentCmd.PersistentFlags().Lookup("server"))
	agentCmd.PersistentFlags().IntP("port", "p", 1234, "Port to connect to")
	viper.BindPFlag("sbcbmc.port", agentCmd.PersistentFlags().Lookup("port"))

	viper.SetDefault("agent.interval", 5)
	viper.SetDefault("agent.tempsensor", "*")
}

func initConfig() {
	if cfgFile != "" {
		viper.SetConfigFile(cfgFile)
	} else {
		// Find home directory.
		home, err := os.UserHomeDir()
		cobra.CheckErr(err)

		// Search config in home directory with name ".cobra" (without extension).
		viper.AddConfigPath("/etc/")
		viper.AddConfigPath(home)
		viper.AddConfigPath(".")
		viper.SetConfigType("yaml")
		viper.SetConfigName("sbcbmc_config.yml")
	}

	viper.AutomaticEnv()

	if err := viper.ReadInConfig(); err == nil {
		hostmonitor.Log.Info("Loading Config", "file", viper.ConfigFileUsed())
	} else {
		hostmonitor.Log.Error(err, "Error Loading Config")
	}
}

func startAgent() {
	hostmonitor.Log.Info("Config:", "server", viper.GetString("sbcbmc.server"), "port", viper.GetInt("sbcbmc.port"))
	hostmonitor.Log.Info("Config:", "debug", viper.GetBool("debug"))

	if len(viper.GetString("sbcbmc.auth")) > 8 {
		hostmonitor.Log.Error(nil, "Auth Token can not be larger than 8 characters")
		os.Exit(-1)
	}
	if viper.GetInt("agent.interval") < 1 {
		hostmonitor.Log.Error(nil, "Interval must be larger than 1 second")
		os.Exit(-1)
	}

	hostmonitor.Log.Info("Starting server")

	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM)
	hostmonitor.StartSendServer()
	hostmonitor.StartSensors()
	<- sigs
	//hostmonitor.StopSensors()
	hostmonitor.Log.Info("Stopping server")
}