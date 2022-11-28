package hostmonitor

import (
	"bufio"
	"net"
	"time"
	"fmt"
	"io"
	"encoding/binary"
)

type EspConn struct {
	conn net.Conn
	shutdown chan bool
	Readbytes chan []byte
	Writebytes chan []byte
}

func (e *EspConn) Init() {
	e.shutdown = make(chan bool)
	e.Readbytes = make(chan []byte, 10)
	e.Writebytes = make(chan []byte, 10)
}

func (e *EspConn) Connect(host string, port int) error {
	var err error
	e.conn, err = net.Dial("tcp", fmt.Sprintf("%s:%d", host, port))
	if err != nil {
		Log.Error(err, "Error connecting to server")
		return err
	}
	go e.readRoutine()
	go e.writeRoutine()
	return nil
}

func (e *EspConn) Disconnect() error {
	if e.conn != nil {
		e.conn.Close()
		e.conn = nil
		e.shutdown <- true
		close(e.shutdown)
	}
	return nil
}

func (e *EspConn) readRoutine() {
	buf := bufio.NewReader(e.conn)
	for {
		select {
		case <- e.shutdown:
			Log.Info("Shutting down read routine")
			return;
		default:
		} 
		e.conn.SetReadDeadline(time.Now().Add(1 * time.Second))
		pcklen := make([]byte, 4)
		for i := 0; i < 4; i++ {
			b, err := buf.ReadByte()
			if err != nil {
				if err, ok := err.(net.Error); ok && err.Timeout() {
					break;
				}
				if (err != io.EOF || i == 0) {
					Log.Error(err, "Error reading from ESP")
					e.shutdown <- true
				}
			}
			pcklen[i] = b
		}
		size := binary.BigEndian.Uint32(pcklen)
		if size > 0 {
			pck := make([]byte, size)
			e.conn.SetReadDeadline(time.Now().Add(1 * time.Second))
			r, err := io.ReadFull(buf, pck)
			if err != nil {
				if err, ok := err.(net.Error); ok && err.Timeout() {
					continue;
				}
				if (err != io.EOF || r == 0) {
					Log.Error(err, "Error reading from ESP")
					e.shutdown <- true
				}
			}
			e.Readbytes <- pck
		}
	}
	// select {
	// case <-e.shutdown:
	// 	Log.Info("Shutting down read routine")
	// 	return
	// }
}

func (e *EspConn) writeRoutine() {
	for {
		select {
		case <-e.shutdown:
			Log.Info("Shutting down write routine")
			return
		case data := <-e.Writebytes:
			sizeBytes := make([]byte, 4)
			binary.BigEndian.PutUint32(sizeBytes, uint32(len(data)))
			_, err := e.conn.Write(sizeBytes)
			if err != nil {
				Log.Error(err, "Error writing to ESP")
				continue; 
			}
			_, err = e.conn.Write(data)
			if err != nil {
				Log.Error(err, "Error writing to ESP")
				continue;
			}
		}
	}
}
