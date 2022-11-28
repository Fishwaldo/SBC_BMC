package hostmonitor

import (
	"bufio"
	"bytes"
	"net"

	"github.com/Fishwaldo/esp32-sbcfanctrl/client/pkg/espmsg"
	//	"google.golang.org/protobuf/proto"
	"github.com/Fishwaldo/esp32-sbcfanctrl/client/pkg/protodelim"
)

type sensorsMsg struct {
	Temp sensorsTempMsg
	CPU  sensorsCPUMsg
}

type sensorsTempMsg struct {
	Temp float64
}

type sensorsCPUMsg struct {
	Load float64
}

var (
	sendMsg chan sensorsMsg
	conn    net.Conn
)

func StartSendServer() error {
	sendMsg = make(chan sensorsMsg, 10)
	var err error
	conn, err = net.Dial("udp", "10.100.200.139:1234")
	if err != nil {
		Log.Error(err, "Error connecting to server. Will Retry")
	}
	go func() {
		for {
			response := espmsg.EspResult{}
			err := protodelim.UnmarshalFrom(bufio.NewReader(conn), &response)
			if err != nil {
				Log.Error(err, "Error reading response from ESP")
			} else {
				Log.Info("Got message from ESP", "Operation", response.Operation.String(), "msg", response)
			}
		}
	}()

	go func() {
		for {
			select {
			case <-quit:
				Log.Info("Stopping send server")
				return
			case msg := <-sendMsg:
				sendMsgToESP(msg)
			}
		}
	}()
	return nil
}

func postUpdate(temp float64, cpu float64) {
	msg := sensorsMsg{}
	msg.Temp.Temp = temp
	msg.CPU.Load = cpu
	sendMsg <- msg
}

func sendMsgToESP(msg sensorsMsg) {

	pbmsg := espmsg.EspMsg{
		Operation: espmsg.EspMsgType_OPSetPerf,
		Id:   1,
		Op: &espmsg.EspMsg_Perf{
			Perf: &espmsg.SetPerf{
				Load: msg.CPU.Load,
				Temp: 65.5,
			},
		},
	}
	raw := &bytes.Buffer{}
	_, err := protodelim.MarshalTo(raw, &pbmsg)
	if err != nil {
		Log.Error(err, "Error marshalling message")
		return
	}
	if conn == nil {
		var err error
		conn, err = net.Dial("udp", "10.100.200.139:1234")
		if err != nil {
			Log.Error(err, "Error connecting to server. Will Retry")
			return
		}
	}
	// var pkt []byte
	// pkt = append(pkt, 0x55)
	// pkt = append(pkt, 0xAA)
	// pkt = append(pkt, raw.Bytes()...)
	//	Log.V(1).Info("Sending message to ESP", "msg", pkt)
	_, err = conn.Write(raw.Bytes())
	if err != nil {
		Log.Error(err, "Error sending message to ESP")
		conn.Close()
		conn = nil
		return
	}
	Log.V(1).Info("Sending message to ESP", "msg", raw.Bytes())
}
