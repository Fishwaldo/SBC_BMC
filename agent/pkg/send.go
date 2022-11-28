package hostmonitor

import (
	"math/rand"

	"github.com/Fishwaldo/esp32-sbcfanctrl/client/pkg/espmsg"
	"google.golang.org/protobuf/proto"
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
	newConn EspConn
)

func sendPB(pbmsg *espmsg.EspReq_Msg) {
	data, err := proto.Marshal(pbmsg)
	if err != nil {
		Log.Error(err, "Error marshalling message")
		return
	}
	Log.Info("Sending message to ESP", "msg", pbmsg)				
	newConn.Writebytes <- data
}

func processResponse(msg *espmsg.EspResult) {
	switch msg.Operation {
	case espmsg.EspMsgType_OpInfo:
		Log.Info("Got Info Packet from ESP", "msg", msg)
		pbmsg := espmsg.EspReq_Msg{
			Operation: espmsg.EspMsgType_OpLogin,
			Op: &espmsg.EspReq_Msg_Login{
				Login: &espmsg.ESPReq_Login{
					Username: "",
					Token: "12345678",
				},
			},
		}
		sendPB(&pbmsg)
	case espmsg.EspMsgType_OpLogin:
		Log.Info("Got Login Packet from ESP", "msg", msg)
		pbmsg2 := espmsg.EspReq_Msg{
			Operation: espmsg.EspMsgType_OpGetConfig,
		}
		sendPB(&pbmsg2)
	case espmsg.EspMsgType_OpGetConfig:
		Log.Info("Got Config Packet from ESP", "msg", msg)
		cfg := msg.GetConfig()
		var i int32
		for i = 0; i < cfg.Channels; i++ {
			pbmsg2 := espmsg.EspReq_Msg{
				Operation: espmsg.EspMsgType_OPGetStatus,
				Id: i,
			}
			sendPB(&pbmsg2)
		}
	case espmsg.EspMsgType_OPGetStatus:
		Log.Info("Got Status Packet from ESP", "msg", msg)
	}
}

func StartSendServer() error {

	newConn.Init();
	err := newConn.Connect("10.100.201.129", 1234)
	if err != nil {
		Log.Error(err, "Error connecting to ESP")
		return err
	}

	go func() {
		for {
			data := <- newConn.Readbytes
			msg := &espmsg.EspResult{}
			err := proto.Unmarshal(data, msg)
			if err != nil {
				Log.Error(err, "Error unmarshalling message")
				continue
			}
			processResponse(msg)
			select {
			case <- quit:
					return
			default:
			}
		}
	}()



	// var err error
	// conn, err = net.Dial("tcp", "10.100.201.129:1234")
	// if err != nil {
	// 	Log.Error(err, "Error connecting to server. Will Retry")
	// }
	// go func() {
	// 	for {
	// 		response := espmsg.EspResult{}
	// 		err := protodelim.UnmarshalFrom(bufio.NewReader(conn), &response)
	// 		if err != nil {
	// 			Log.Error(err, "Error reading response from ESP")
	// 			quit <- true
	// 			return
	// 		} else {
	// 			Log.Info("Got message from ESP", "Operation", response.Operation.String(), "msg", &response)
	// 			processResponse(&response);
	// 		}
	// 	}
	// }()

	// go func() {
	// 	for {
	// 		select {
	// 		case <-quit:
	// 			Log.Info("Stopping send server")
	// 			return
	// 		case msg := <-sendMsg:
	// 			sendMsgToESP(msg)
	// 		}
	// 	}
	// }()
	return nil
}

func postUpdate(temp float64, cpu float64) {
	msg := sensorsMsg{}
	msg.Temp.Temp = temp
	msg.CPU.Load = cpu
	sendMsgToESP(msg)
}

func sendMsgToESP(msg sensorsMsg) {
	pbmsg := espmsg.EspReq_Msg{
		Operation: espmsg.EspMsgType_OPSetPerf,
		Id:   int32(rand.Intn(6)),
		Op: &espmsg.EspReq_Msg_Perf{
			Perf: &espmsg.ESPReq_SetPerf{
				Load: float32(msg.CPU.Load),
				Temp: float32((rand.Intn(80-40) + 40)),
			},
		},
	}
	sendPB(&pbmsg)
}

