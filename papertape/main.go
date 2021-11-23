package main

import (
	"bufio"
	"fmt"
	"github.com/tarm/serial"
	"log"
	"os"
	"strings"
	"time"
)

func main() {
	if (len(os.Args) < 3) {
		fmt.Printf("Please supply a device name and a filename to upload\n")
		return
	}
	c := &serial.Config{Name: os.Args[1], Baud: 9600 }
	s, err := serial.OpenPort(c)
	if err != nil {
		log.Fatal(err)
		return
	}
//	go readSerial(s)

	s.Write([]byte("\n"))
	time.Sleep(time.Second)

	s.Write([]byte("L"))

	f, err := os.Open(os.Args[2])
	if err != nil {
		fmt.Printf("Error opening file: %+v\n", err)
		return
	}

	scanner := bufio.NewScanner(f)
	scanner.Split(bufio.ScanLines)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())+"\r\n"
		fmt.Printf("Sending: %s", line)
		_, err = s.Write([]byte(line))
		if err != nil {
			fmt.Printf("Error writing bytes: %+v\n", err)
		}
		readline(s)
	}
	s.Write([]byte{4})
	fmt.Printf("Done.\n")
}

func readSerial(s *serial.Port) {
	b := make([]byte, 1)

	for {
		_, err := s.Read(b)
		if err != nil {
			fmt.Printf("Error reading port: %+v\n", err)
			return
		} else {
			os.Stdout.Write(b)
		}
	}
}

func readline(s *serial.Port) {
	b := make([]byte, 1)
	gotSemi := false
	for {
		_, err := s.Read(b)
		if err != nil {
			fmt.Printf("Error reading port: %+v\n", err)
			return
		} else {
			if gotSemi && b[0] == 10 {
				return
			} else if !gotSemi && b[0] == ';' {
				gotSemi  = true
			}

		}
	}
}
